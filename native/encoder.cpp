#include "cli.hpp"
#include "core.hpp"
#include "flac_audio.hpp"
#include "original_file.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "container.hpp"
namespace fs = std::filesystem;
using mjxl::Movie;
using mjxl::read_movie;
using mjxl::read_original_chunk;
using mjxl::fourcc;
using mjxl::MJXL;

static void check(bool ok, const char* error)
{
    if (!ok) {
        throw std::runtime_error(error);
    }
}

static std::string utf8(const fs::path& path)
{
    auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// 저장 형식과 무관하게 모든 오디오를 원본 PCM 바이트로 복원한다.
static std::vector<uint8_t> pcm_of(const Movie& movie)
{
    std::vector<uint8_t> pcm;
    std::unique_ptr<mjxl::FlacDecoder> decoder;
    if (movie.flac) {
        decoder = std::make_unique<mjxl::FlacDecoder>(movie.streamInfo, mjxl::PcmFormat{movie.bits, movie.channels, movie.sampleRate});
    }
    for (const auto& p : movie.packets) {
        if (!p.video) {
            const auto bytes = decoder ? decoder->decode(p.bytes) : p.bytes;
            pcm.insert(pcm.end(), bytes.begin(), bytes.end());
        }
    }
    if (decoder) {
        const auto bytes = decoder->flush();
        pcm.insert(pcm.end(), bytes.begin(), bytes.end());
    }
    return pcm;
}

static void verify(const Movie& source, const fs::path& encoded, const mjxl::Codec& codec, bool expectedJxl = true)
{
    const auto target = read_movie(encoded);
    check(target.jxl == expectedJxl && target.width == source.width && target.height == source.height && int64_t(target.fps.num) * source.fps.den == int64_t(source.fps.num) * target.fps.den, "Video format verification failed");
    check(target.bits == source.bits && target.channels == source.channels && target.sampleRate == source.sampleRate, "Audio format verification failed");
    std::vector<std::vector<uint8_t>> originalFrames, restoredFrames;
    for (const auto& p : source.packets) {
        if (p.video) { originalFrames.push_back(source.jxl ? codec.jpeg(p.bytes) : p.bytes); }
    }
    for (const auto& p : target.packets) {
        if (p.video) { restoredFrames.push_back(target.jxl ? codec.jpeg(p.bytes) : p.bytes); }
    }
    const auto originalAudio = pcm_of(source);
    const auto restoredAudio = pcm_of(target);
    check(originalFrames == restoredFrames, "Original JPEG packet verification failed");
    check(originalAudio == restoredAudio, "PCM sample verification failed");
    std::cout << "Verified " << originalFrames.size() << " exact JPEG frames and " << originalAudio.size() << " exact PCM bytes\n";
}

static std::vector<uint8_t> read_file(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    check(bool(file), "Cannot open file");
    const auto size = static_cast<std::streamoff>(file.tellg());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (size) { file.read(reinterpret_cast<char*>(bytes.data()), size); }
    check(bool(file), "Cannot read file");
    return bytes;
}

// 복원 데이터를 만들지 못하면 원본 복원 기능을 조용히 생략하지 않고 실패한다.
static std::vector<uint8_t> make_original_data(const Movie& movie, std::span<const uint8_t> file)
{
    constexpr auto hint = "; the original file could not be restored. Use --no-original to encode without it";
    std::vector<mjxl::OriginalPacket> packets;
    packets.reserve(movie.packets.size());
    for (const auto& p : movie.packets) {
        const bool found = p.position >= 0 && uint64_t(p.position) <= file.size() && p.bytes.size() <= file.size() - uint64_t(p.position) &&
                           std::equal(p.bytes.begin(), p.bytes.end(), file.begin() + static_cast<ptrdiff_t>(p.position));
        if (!found) {
            throw std::runtime_error(std::string("Media packets differ from the input file") + hint);
        }
        packets.push_back({p.video, static_cast<uint64_t>(p.position), static_cast<uint32_t>(p.bytes.size())});
    }
    try {
        return mjxl::pack_original(file, packets);
    } catch (const std::exception& error) {
        throw std::runtime_error(error.what() + std::string(hint));
    }
}

// 인코딩된 컨테이너의 정보만으로 원본 파일 전체를 복원한다.
static std::vector<uint8_t> rebuild_from_container(const Movie& encoded, std::span<const uint8_t> payload, const mjxl::Codec& codec)
{
    std::vector<std::vector<uint8_t>> frames;
    for (const auto& p : encoded.packets) {
        if (p.video) { frames.push_back(codec.jpeg(p.bytes)); }
    }
    return mjxl::rebuild_original(payload, frames, pcm_of(encoded));
}

// 내부 UTF-8 문자열로 경로를 만들 때 플랫폼 기본 인코딩을 사용하지 않는다.
static fs::path to_path(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

static void print_size_row(const std::string& label, uint64_t original, uint64_t stored)
{
    const auto bytes = [](uint64_t value) {
        auto text = std::to_string(value);
        for (size_t i = text.size(); i > 3; i -= 3) {
            text.insert(i - 3, ",");
        }
        return text;
    };
    const auto reduction = original == 0 ? "n/a" :
        std::format("{:.2f}%", 100.0 * (1.0 - static_cast<double>(stored) / static_cast<double>(original)));
    std::cout << std::format("{:<20} {:>18} -> {:>18} {:>12}\n", label, bytes(original), bytes(stored), reduction);
}

static int run(const std::vector<std::string>& arguments)
{
    mjxl::configure_container_logging();
    try {
        const auto cli = mjxl::parse_command_line(arguments);
        if (cli.help) {
            std::cout << mjxl::help_text(cli.command);
            return cli.missingCommand ? 1 : 0;
        }
        const auto& command = cli.command;
        const bool noOriginal = cli.noOriginal;
        const int effort = cli.effort;
        const fs::path firstPath = to_path(cli.paths[0]);
        const fs::path secondPath = cli.paths.size() > 1 ? to_path(cli.paths[1]) : fs::path{};
        const auto movie = read_movie(firstPath);
        mjxl::Codec codec;
        if (command == "encode") {
            const bool keepOriginal = !noOriginal;
            check(!movie.jxl, "encode requires an MJPEG input");
            const fs::path output(secondPath);
            check(!fs::exists(output), "Output already exists");
            auto extension = output.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            check(extension == ".avi" || extension == ".mkv", "Output extension must be .avi or .mkv");
            const bool matroska = extension == ".mkv";
            auto partial = output; partial += ".partial";
            const mjxl::PcmFormat format{movie.bits, movie.channels, movie.sampleRate};
            const auto pcm = pcm_of(movie);
            std::optional<mjxl::FlacStream> flac;
            const auto selection = movie.channels ? mjxl::select_audio(
                matroska ? mjxl::Container::Matroska : mjxl::Container::Avi, cli.audioCodec, format) :
                mjxl::AudioSelection{mjxl::AudioCodec::Copy, "No audio track"};
            check(selection.codec != mjxl::AudioCodec::Alac, "ALAC is planned but is not implemented in this build");
            std::cout << "Audio policy: " << (movie.channels ? mjxl::audio_codec_name(selection.codec) : "none")
                      << " (" << selection.reason << "), backend: native-audio/FFmpeg-container\n";
            if (selection.codec == mjxl::AudioCodec::Flac && !pcm.empty()) {
                flac = mjxl::flac_encode(pcm, format);
                // 파일을 쓰기 전에 FLAC에서 원본 PCM으로 정확히 복원되는지 확인한다.
                mjxl::FlacDecoder decoder(flac->streamInfo, format);
                std::vector<uint8_t> restored;
                for (const auto& packet : flac->packets) {
                    const auto bytes = decoder.decode(packet.bytes);
                    restored.insert(restored.end(), bytes.begin(), bytes.end());
                }
                const auto rest = decoder.flush();
                restored.insert(restored.end(), rest.begin(), rest.end());
                check(restored == pcm, "FLAC round-trip verification failed");
                std::cout << "Audio FLAC: " << pcm.size() << " PCM bytes stored as " << flac->storedBits << "-bit FLAC\n";
            } else if (selection.codec == mjxl::AudioCodec::Copy && !pcm.empty()) {
                std::cout << "Audio stays PCM: " << pcm.size() << " bytes\n";
            }
            const auto sourceFile = read_file(firstPath);
            const auto originalData = keepOriginal ? make_original_data(movie, sourceFile) : std::vector<uint8_t>{};
            if (!keepOriginal) {
                std::cout << "WARNING: This output cannot restore the original file; keep the original\n";
            }
            std::unique_ptr<mjxl::ContainerWriter> avi;
            std::unique_ptr<mjxl::ContainerWriter> mkv;
            if (matroska) {
                mkv = mjxl::make_matroska_writer(partial, movie, flac ? &*flac : nullptr, originalData);
            } else {
                avi = mjxl::make_avi_writer(partial, movie, MJXL, flac ? &*flac : nullptr);
            }
            const auto write_packet = [&](bool video, std::span<const uint8_t> bytes) {
                if (mkv) {
                    mkv->packet(video, bytes);
                } else {
                    avi->packet(video, bytes);
                }
            };
            unsigned frames = 0;
            uint64_t originalVideoBytes = 0, originalAudioBytes = 0;
            uint64_t storedVideoBytes = 0, storedAudioBytes = 0;
            size_t nextAudio = 0;
            // 재생 시각이 도래한 FLAC 프레임을 해당 영상 프레임보다 먼저 쓴다.
            const auto write_audio_until = [&](int64_t frame) {
                while (flac && nextAudio < flac->packets.size() &&
                       flac->packets[nextAudio].position * movie.fps.num <= frame * movie.fps.den * movie.sampleRate) {
                    write_packet(false, flac->packets[nextAudio].bytes);
                    storedAudioBytes += flac->packets[nextAudio].bytes.size();
                    ++nextAudio;
                }
            };
            for (const auto& p : movie.packets) {
                if (p.video) {
                    originalVideoBytes += p.bytes.size();
                    write_audio_until(frames);
                    const auto encoded = codec.encode(p.bytes, effort);
                    check(codec.jpeg(encoded) == p.bytes, "JPEG round-trip verification failed");
                    write_packet(true, encoded);
                    storedVideoBytes += encoded.size();
                    ++frames;
                    if (frames % 60 == 0) { std::cout << "Encoded " << frames << " frames\n"; }
                } else {
                    originalAudioBytes += p.bytes.size();
                    if (!flac) {
                        write_packet(false, p.bytes);
                        storedAudioBytes += p.bytes.size();
                    }
                }
            }
            if (flac) {
                // 마지막 영상 프레임 뒤에 남은 오디오를 모두 쓴다.
                write_audio_until(INT64_MAX / (int64_t(movie.fps.den) * movie.sampleRate));
            }
            if (mkv) {
                mkv->finish();
            } else {
                avi->finish(originalData);
            }
            verify(movie, partial, codec);
            if (!originalData.empty()) {
                // 최종 이름을 확정하기 전에 저장한 컨테이너에서 원본 전체를 복원해 검증한다.
                const auto stored = read_original_chunk(partial);
                check(rebuild_from_container(read_movie(partial), stored, codec) == sourceFile, "Original file verification failed");
                std::cout << "Verified exact original file: " << sourceFile.size() << " bytes, stored " << originalData.size() << " bytes of recovery data\n";
            }
            fs::rename(partial, output);
            const auto savedSize = fs::file_size(output);
            std::cout << "\nCompression summary (bytes; negative reduction means growth):\n";
            std::cout << std::format("{:<20} {:>18}    {:>18} {:>12}\n", "Component", "Original", "Stored", "Reduction");
            print_size_row("Audio", originalAudioBytes, storedAudioBytes);
            print_size_row("Video", originalVideoBytes, storedVideoBytes);
            check(storedVideoBytes <= savedSize && storedAudioBytes <= savedSize - storedVideoBytes &&
                  originalData.size() <= savedSize - storedVideoBytes - storedAudioBytes, "Invalid component byte counts");
            print_size_row("Recovery payload", 0, originalData.size());
            print_size_row("Container", sourceFile.size() - originalVideoBytes - originalAudioBytes,
                           savedSize - storedVideoBytes - storedAudioBytes - originalData.size());
            print_size_row("Total file", sourceFile.size(), savedSize);
            std::cout << "Saved: " << utf8(output) << '\n';
        } else if (command == "restore") {
            check(movie.jxl, "restore requires an MJXL input");
            const fs::path output(secondPath);
            check(!fs::exists(output), "Output already exists");
            auto extension = output.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            check(extension == ".avi", "restore writes MJPEG AVI; output extension must be .avi");
            auto partial = output; partial += ".partial";
            auto writer = mjxl::make_avi_writer(partial, movie, fourcc('M', 'J', 'P', 'G'));
            std::unique_ptr<mjxl::FlacDecoder> decoder;
            if (movie.flac) {
                decoder = std::make_unique<mjxl::FlacDecoder>(movie.streamInfo, mjxl::PcmFormat{movie.bits, movie.channels, movie.sampleRate});
            }
            for (const auto& p : movie.packets) {
                if (p.video) {
                    const auto jpeg = codec.jpeg(p.bytes);
                    writer->packet(true, jpeg);
                } else if (decoder) {
                    const auto pcm = decoder->decode(p.bytes);
                    if (!pcm.empty()) { writer->packet(false, pcm); }
                } else {
                    writer->packet(false, p.bytes);
                }
            }
            if (decoder) {
                const auto pcm = decoder->flush();
                if (!pcm.empty()) { writer->packet(false, pcm); }
            }
            writer->finish();
            verify(movie, partial, codec, false);
            fs::rename(partial, output);
            std::cout << "Restored exact JPEG packets and PCM samples to " << utf8(output) << '\n';
        } else if (command == "restore-original") {
            check(movie.jxl, "restore-original requires an MJXL input");
            const fs::path output(secondPath);
            check(!fs::exists(output), "Output already exists");
            auto partial = output; partial += ".partial";
            check(!fs::exists(partial), "Partial output already exists");
            const auto stored = read_original_chunk(firstPath);
            check(!stored.empty(), "This file holds no original file data; encode it again from the original file");
            const auto rebuilt = rebuild_from_container(movie, stored, codec);
            {
                std::ofstream file(partial, std::ios::binary);
                file.write(reinterpret_cast<const char*>(rebuilt.data()), static_cast<std::streamsize>(rebuilt.size()));
                check(bool(file), "Cannot write restored file");
            }
            check(read_file(partial) == rebuilt, "Restored file verification failed");
            fs::rename(partial, output);
            std::cout << "Restored exact original file (SHA-256 verified): " << rebuilt.size() << " bytes to " << utf8(output) << '\n';
        } else if (command == "verify") {
            verify(movie, secondPath, codec);
            const auto stored = read_original_chunk(secondPath);
            if (stored.empty()) {
                // 원본 삭제 판단에 쓰이는 검사이므로 완전 복원이 불가능하면 실패한다.
                check(noOriginal, "This file holds no original file data, so the original file cannot be restored from it");
                std::cout << "WARNING: This file holds no original file data; keep the original\n";
            } else {
                check(rebuild_from_container(read_movie(secondPath), stored, codec) == read_file(firstPath), "Original file verification failed");
                std::cout << "Verified exact original file\n";
            }
        } else if (command == "bench") {
            check(movie.jxl, "bench requires an MJXL file");
            const auto start = std::chrono::steady_clock::now();
            unsigned frames = 0;
            for (const auto& p : movie.packets) {
                if (p.video) {
                    const auto pixels = codec.rgb(p.bytes, movie.width, movie.height);
                    check(!pixels.empty(), "Empty decoded frame");
                    ++frames;
                }
            }
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << frames << " RGB frames in " << seconds << " s, " << frames / seconds << " fps\n";
        } else {
            throw std::runtime_error("Unknown command");
        }
        return 0;
    } catch (const mjxl::UsageError& error) {
        std::cerr << "Error: " << error.what() << "\nRun \"mjxl --help\" for usage.\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
// Windows UTF-16 인수를 UTF-8로 바꾸고 콘솔도 UTF-8로 설정한다.
int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        const int length = static_cast<int>(wcslen(argv[i]));
        std::string text;
        if (length) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], length, nullptr, 0, nullptr, nullptr);
            text.resize(static_cast<size_t>(size));
            WideCharToMultiByte(CP_UTF8, 0, argv[i], length, text.data(), size, nullptr, nullptr);
        }
        arguments.push_back(std::move(text));
    }
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
