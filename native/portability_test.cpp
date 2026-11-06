#include "audio_policy.hpp"
#include "alac_audio.hpp"
#include "cli.hpp"
#include "original_file.hpp"
#include "third_party/picosha2/picosha2.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) {
        throw std::runtime_error(message);
    }
}

template<class Function>
void rejects(Function function)
{
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Invalid input was accepted");
}

void check_policy()
{
    using namespace mjxl;
    const PcmFormat pcm{16, 2, 44100};
    require(select_audio(Container::Mov, AudioCodec::Auto, {16, 4, 44100, 0x33}).codec == AudioCodec::Copy,
            "Unsupported ALAC speaker layout did not fall back to PCM");
    rejects([]() { select_audio(Container::Mov, AudioCodec::Alac, {16, 4, 44100, 0x33}); });
    require(select_audio(Container::Mov, AudioCodec::Auto, pcm).codec == AudioCodec::Alac, "MOV policy differs");
    for (const auto container : {Container::Avi, Container::Matroska}) {
        require(select_audio(container, AudioCodec::Auto, pcm).codec == AudioCodec::Flac, "FLAC default differs");
        require(select_audio(container, AudioCodec::Auto, {32, 2, 48000}).codec == AudioCodec::Copy, "PCM32 fallback differs");
        rejects([&]() { select_audio(container, AudioCodec::Flac, {32, 2, 48000}); });
    }
    require(parse_command_line({"encode", "in", "out", "--audio-codec=copy", "--copy-audio"}).audioCodec == AudioCodec::Copy,
            "Copy alias differs");
    require(parse_command_line({"encode", "--audio-codec", "flac", "in", "out"}).audioCodec == AudioCodec::Flac,
            "Explicit FLAC differs");
    rejects([]() { parse_command_line({"encode", "in", "out", "--audio-codec", "bad"}); });
    rejects([]() { parse_command_line({"encode", "in", "out", "--audio-codec"}); });
    rejects([]() { parse_command_line({"encode", "in", "out", "--copy-audio", "--audio-codec=auto"}); });
    rejects([]() { parse_command_line({"encode", "in", "out", "--audio-codec=flac", "--copy-audio"}); });
    rejects([]() { parse_command_line({"verify", "in", "out", "--audio-codec=copy"}); });
}

void check_hash_and_archive()
{
    const std::string abc = "abc";
    require(picosha2::hash256_hex_string(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 known-answer mismatch");
    const std::string empty;
    require(picosha2::hash256_hex_string(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "Empty SHA-256 mismatch");
    const std::vector<uint8_t> original{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const auto payload = mjxl::pack_original(original, {{true, 2, 3}, {false, 7, 2}});
    require(mjxl::rebuild_original(payload, {{2, 3, 4}}, std::array<uint8_t, 2>{7, 8}) == original, "Archive round trip differs");
    rejects([&]() { mjxl::rebuild_original(payload, {{2, 3, 5}}, std::array<uint8_t, 2>{7, 8}); });
}

void check_alac()
{
    unsigned cases = 0;
    for (const unsigned depth : {8u, 16u, 24u, 32u}) {
        for (const unsigned channels : {1u, 2u, 3u, 6u, 8u}) {
            for (const unsigned count : {1u, 5003u}) {
                const mjxl::PcmFormat format{depth, channels, 44100};
                std::vector<uint8_t> pcm(size_t(count) * channels * depth / 8);
                uint32_t random = 1234567;
                for (auto& byte : pcm) {
                    random = random * 1664525u + 1013904223u;
                    byte = static_cast<uint8_t>(random >> 24);
                }
                const auto encoded = mjxl::alac_encode(pcm, format);
                mjxl::AlacDecoder decoder(encoded.streamInfo, format);
                std::vector<uint8_t> decoded;
                for (const auto& packet : encoded.packets) {
                    const auto part = decoder.decode(packet.bytes);
                    decoded.insert(decoded.end(), part.begin(), part.end());
                }
                require(decoded == pcm, "ALAC round trip differs");
                const auto& packet = encoded.packets.front().bytes;
                rejects([&]() { decoder.decode(std::span<const uint8_t>(packet).first(packet.size() / 2)); });
                auto badConfig = encoded.streamInfo;
                badConfig[0] = 0xff;
                rejects([&]() { mjxl::AlacDecoder invalid(badConfig, format); });
                ++cases;
            }
        }
    }
    std::cout << "Passed " << cases << " ALAC depth/channel/length cases\n";
}

void check_flac()
{
    unsigned cases = 0;
    for (const unsigned bits : {8u, 16u, 24u}) {
        for (const unsigned channels : {1u, 2u, 8u}) {
            for (const unsigned rate : {8040u, 44100u, 48000u}) {
                for (const unsigned count : {1u, 5003u}) {
                    const mjxl::PcmFormat format{bits, channels, rate};
                    std::vector<uint8_t> pcm(size_t(count) * channels * (bits / 8));
                    uint32_t random = 0x12345678;
                    for (auto& byte : pcm) {
                        random = random * 1664525u + 1013904223u;
                        byte = static_cast<uint8_t>(random >> 24);
                    }
                    const auto encoded = mjxl::flac_encode(pcm, format);
                    mjxl::FlacDecoder decoder(encoded.streamInfo, format);
                    std::vector<uint8_t> output;
                    for (const auto& packet : encoded.packets) {
                        const auto part = decoder.decode(packet.bytes);
                        output.insert(output.end(), part.begin(), part.end());
                    }
                    require(decoder.flush().empty() && output == pcm, "FLAC round trip differs");
                    auto corrupt = encoded.packets.front().bytes;
                    corrupt.back() ^= 1;
                    rejects([&]() { decoder.decode(corrupt); });
                    auto truncated = encoded.packets.front().bytes;
                    truncated.resize(truncated.size() / 2);
                    rejects([&]() { decoder.decode(truncated); });
                    ++cases;
                }
            }
        }
    }
    std::cout << "Passed " << cases << " FLAC formats/lengths and corrupt/truncated packet rejection\n";
}
}

int main()
{
    try {
        check_policy();
        check_hash_and_archive();
        check_flac();
        check_alac();
        std::cout << "Passed policy, CLI, SHA-256 and archive checks without FFmpeg\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
