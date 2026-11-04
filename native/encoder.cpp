#include "core.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace fs = std::filesystem;
constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint8_t(a) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
constexpr auto MJXL = fourcc('M', 'J', 'X', 'L');
struct Packet {
    bool video;
    std::vector<uint8_t> bytes;
};
struct Movie {
    unsigned width = 0, height = 0, sampleRate = 0, channels = 0, bits = 0;
    AVRational fps{};
    bool jxl = false;
    std::vector<Packet> packets;
};

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

static Movie read_movie(const fs::path& path)
{
    AVFormatContext* raw = nullptr;
    check(avformat_open_input(&raw, utf8(path).c_str(), nullptr, nullptr) >= 0, "Cannot open input video");
    auto closer = [](AVFormatContext* ptr) { avformat_close_input(&ptr); };
    std::unique_ptr<AVFormatContext, decltype(closer)> input(raw, closer);
    check(avformat_find_stream_info(raw, nullptr) >= 0, "Cannot inspect input streams");
    int video = -1, audio = -1;
    Movie movie;
    for (unsigned i = 0; i < raw->nb_streams; ++i) {
        const auto stream = raw->streams[i];
        const auto info = stream->codecpar;
        if (info->codec_type == AVMEDIA_TYPE_VIDEO) {
            check(video < 0, "Multiple video streams are not supported");
            check(info->codec_id == AV_CODEC_ID_MJPEG || info->codec_tag == MJXL, "Input must be MJPEG or experimental MJXL AVI");
            video = static_cast<int>(i);
            movie.jxl = info->codec_tag == MJXL;
            movie.width = info->width;
            movie.height = info->height;
            movie.fps = av_guess_frame_rate(raw, stream, nullptr);
            check(movie.fps.num > 0 && movie.fps.den > 0, "Cannot determine constant frame rate");
        } else if (info->codec_type == AVMEDIA_TYPE_AUDIO) {
            check(audio < 0, "Multiple audio streams are not supported");
            check(info->codec_id == AV_CODEC_ID_PCM_U8 || info->codec_id == AV_CODEC_ID_PCM_S16LE || info->codec_id == AV_CODEC_ID_PCM_S24LE || info->codec_id == AV_CODEC_ID_PCM_S32LE, "Audio must be unsigned PCM8 or signed little-endian PCM16/24/32");
            audio = static_cast<int>(i);
            movie.sampleRate = info->sample_rate;
            movie.channels = info->ch_layout.nb_channels;
            movie.bits = av_get_bits_per_sample(info->codec_id);
            check(movie.sampleRate > 0 && movie.channels > 0 && movie.channels <= 8, "Invalid PCM format");
        } else {
            throw std::runtime_error("Additional streams would be lost; input is unsupported");
        }
        check(stream->start_time == AV_NOPTS_VALUE || stream->start_time == 0, "Nonzero stream start offsets are not supported");
    }
    check(video >= 0 && movie.width > 0 && movie.height > 0 && movie.width <= 16384 && movie.height <= 16384, "Invalid video stream");
    AVPacket* p = av_packet_alloc();
    check(p != nullptr, "Cannot allocate packet");
    size_t frame = 0;
    int status = 0;
    try {
        while ((status = av_read_frame(raw, p)) >= 0) {
            if (p->stream_index == video || p->stream_index == audio) {
                const bool isVideo = p->stream_index == video;
                if (isVideo) {
                    if (p->pts != AV_NOPTS_VALUE) {
                        const auto expected = av_rescale_q(static_cast<int64_t>(frame), av_inv_q(movie.fps), raw->streams[video]->time_base);
                        check(std::abs(p->pts - expected) <= 1, "Variable frame rate is not supported by this AVI profile");
                    }
                    ++frame;
                }
                check(p->size > 0 && p->size < 256 * 1024 * 1024, "Invalid packet size");
                movie.packets.push_back({isVideo, {p->data, p->data + p->size}});
            }
            av_packet_unref(p);
        }
        check(status == AVERROR_EOF, "Input packet read failed");
        av_packet_free(&p);
    } catch (...) {
        av_packet_free(&p);
        throw;
    }
    check(frame != 0, "No video frames");
    return movie;
}

class AviWriter {
    std::fstream file_;
    uint64_t movi_ = 0, frameCountPos_ = 0, videoLengthPos_ = 0, audioLengthPos_ = 0;
    uint32_t frames_ = 0, samples_ = 0, blockAlign_ = 0;
    struct Entry { uint32_t id, flags, offset, size; };
    std::vector<Entry> index_;
public:
    explicit AviWriter(const fs::path& path, const Movie& movie, uint32_t videoCodec = MJXL)
    {
        check(!fs::exists(path), "Output or partial output already exists");
        file_.open(path, std::ios::binary | std::ios::out | std::ios::in | std::ios::trunc);
        check(bool(file_), "Cannot create AVI output");
        tag("RIFF"); u32(0); tag("AVI ");
        const auto hdrl = list("hdrl");
        tag("avih"); u32(56);
        u32(static_cast<uint32_t>(1000000.0 * movie.fps.den / movie.fps.num + 0.5));
        u32(0); u32(0); u32(0x10);
        frameCountPos_ = position(); u32(0);
        u32(0); u32(movie.channels ? 2 : 1); u32(movie.width * movie.height * 3);
        u32(movie.width); u32(movie.height);
        for (int i = 0; i < 4; ++i) { u32(0); }
        auto stream = list("strl");
        tag("strh"); u32(56); tag("vids"); u32(videoCodec); u32(0); u16(0); u16(0); u32(0);
        u32(movie.fps.den); u32(movie.fps.num); u32(0);
        videoLengthPos_ = position(); u32(0);
        u32(movie.width * movie.height * 3); u32(0xffffffff); u32(0);
        u16(0); u16(0); u16(static_cast<uint16_t>(movie.width)); u16(static_cast<uint16_t>(movie.height));
        tag("strf"); u32(40); u32(40); u32(movie.width); u32(movie.height); u16(1); u16(24);
        u32(videoCodec); u32(movie.width * movie.height * 3); u32(0); u32(0); u32(0); u32(0);
        finish_list(stream);
        if (movie.channels) {
            blockAlign_ = movie.channels * movie.bits / 8;
            stream = list("strl");
            tag("strh"); u32(56); tag("auds"); u32(0); u32(0); u16(0); u16(0); u32(0);
            u32(blockAlign_); u32(movie.sampleRate * blockAlign_); u32(0);
            audioLengthPos_ = position(); u32(0);
            u32(65536); u32(0xffffffff); u32(blockAlign_);
            for (int i = 0; i < 4; ++i) { u16(0); }
            tag("strf"); u32(18); u16(1); u16(static_cast<uint16_t>(movie.channels));
            u32(movie.sampleRate); u32(movie.sampleRate * blockAlign_);
            u16(static_cast<uint16_t>(blockAlign_)); u16(static_cast<uint16_t>(movie.bits)); u16(0);
            finish_list(stream);
        }
        finish_list(hdrl);
        movi_ = list("movi");
    }

    uint64_t position()
    {
        return static_cast<uint64_t>(file_.tellp());
    }
    void bytes(const void* data, size_t count)
    {
        check(position() + count < 0x7ffff000ULL, "This AVI profile is limited to 2 GiB");
        file_.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
        check(bool(file_), "AVI write failed");
    }
    void u32(uint32_t value)
    {
        bytes(&value, sizeof(value));
    }
    void u16(uint16_t value)
    {
        bytes(&value, sizeof(value));
    }
    void tag(const char* value)
    {
        bytes(value, 4);
    }
    uint64_t list(const char* type)
    {
        tag("LIST"); const auto at = position(); u32(0); tag(type); return at;
    }
    void patch(uint64_t at, uint32_t value)
    {
        const auto end = position(); file_.seekp(at); u32(value); file_.seekp(end);
    }
    void finish_list(uint64_t at)
    {
        patch(at, static_cast<uint32_t>(position() - at - 4));
    }
    void packet(bool video, std::span<const uint8_t> data)
    {
        const auto offset = position() - (movi_ + 4);
        const uint32_t id = video ? fourcc('0', '0', 'd', 'c') : fourcc('0', '1', 'w', 'b');
        u32(id); u32(static_cast<uint32_t>(data.size())); bytes(data.data(), data.size());
        if (data.size() & 1) { const char zero = 0; bytes(&zero, 1); }
        index_.push_back({id, 0x10, static_cast<uint32_t>(offset), static_cast<uint32_t>(data.size())});
        if (video) {
            ++frames_;
        } else {
            check(blockAlign_ && data.size() % blockAlign_ == 0, "PCM packet is not sample aligned");
            samples_ += static_cast<uint32_t>(data.size() / blockAlign_);
        }
    }
    void finish()
    {
        finish_list(movi_);
        tag("idx1"); u32(static_cast<uint32_t>(index_.size() * sizeof(Entry)));
        bytes(index_.data(), index_.size() * sizeof(Entry));
        patch(frameCountPos_, frames_); patch(videoLengthPos_, frames_);
        if (audioLengthPos_) { patch(audioLengthPos_, samples_); }
        patch(4, static_cast<uint32_t>(position() - 8));
        file_.close();
    }
};

static void verify(const Movie& source, const fs::path& encoded, const mjxl::Codec& codec, bool expectedJxl = true)
{
    const auto target = read_movie(encoded);
    check(target.jxl == expectedJxl && target.width == source.width && target.height == source.height && av_cmp_q(target.fps, source.fps) == 0, "Video format verification failed");
    check(target.bits == source.bits && target.channels == source.channels && target.sampleRate == source.sampleRate, "Audio format verification failed");
    std::vector<std::vector<uint8_t>> originalFrames, restoredFrames;
    std::vector<uint8_t> originalAudio, restoredAudio;
    for (const auto& p : source.packets) {
        if (p.video) { originalFrames.push_back(source.jxl ? codec.jpeg(p.bytes) : p.bytes); }
        else { originalAudio.insert(originalAudio.end(), p.bytes.begin(), p.bytes.end()); }
    }
    for (const auto& p : target.packets) {
        if (p.video) { restoredFrames.push_back(target.jxl ? codec.jpeg(p.bytes) : p.bytes); }
        else { restoredAudio.insert(restoredAudio.end(), p.bytes.begin(), p.bytes.end()); }
    }
    check(originalFrames == restoredFrames, "Original JPEG packet verification failed");
    check(originalAudio == restoredAudio, "PCM sample verification failed");
    std::cout << "Verified " << originalFrames.size() << " exact JPEG frames and " << originalAudio.size() << " exact PCM bytes\n";
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8); av_log_set_level(AV_LOG_ERROR);
    try {
        check(argc >= 3, "Usage: mjxl encode input.mov output.avi | restore encoded.avi mjpeg.avi | verify original.mov encoded.avi | bench encoded.avi");
        const std::wstring command = argv[1];
        const auto movie = read_movie(argv[2]);
        mjxl::Codec codec;
        if (command == L"encode") {
            check(argc == 4 && !movie.jxl, "encode requires an MJPEG input and an AVI output");
            const fs::path output(argv[3]);
            check(!fs::exists(output), "Output already exists");
            auto partial = output; partial += L".partial";
            AviWriter writer(partial, movie);
            unsigned frames = 0;
            for (const auto& p : movie.packets) {
                if (p.video) {
                    const auto encoded = codec.encode(p.bytes);
                    check(codec.jpeg(encoded) == p.bytes, "JPEG round-trip verification failed");
                    writer.packet(true, encoded);
                    ++frames;
                    if (frames % 60 == 0) { std::cout << "Encoded " << frames << " frames\n"; }
                } else {
                    writer.packet(false, p.bytes);
                }
            }
            writer.finish();
            verify(movie, partial, codec);
            fs::rename(partial, output);
            std::cout << "Saved " << fs::file_size(output) << " bytes: " << utf8(output) << '\n';
        } else if (command == L"restore") {
            check(argc == 4 && movie.jxl, "restore requires an MJXL AVI input and an MJPEG AVI output");
            const fs::path output(argv[3]);
            check(!fs::exists(output), "Output already exists");
            auto partial = output; partial += L".partial";
            AviWriter writer(partial, movie, fourcc('M', 'J', 'P', 'G'));
            for (const auto& p : movie.packets) {
                if (p.video) {
                    const auto jpeg = codec.jpeg(p.bytes);
                    writer.packet(true, jpeg);
                } else {
                    writer.packet(false, p.bytes);
                }
            }
            writer.finish();
            verify(movie, partial, codec, false);
            fs::rename(partial, output);
            std::cout << "Restored exact JPEG packets and PCM samples to " << utf8(output) << '\n';
        } else if (command == L"verify") {
            check(argc == 4, "verify requires original and encoded paths");
            verify(movie, argv[3], codec);
        } else if (command == L"bench") {
            check(movie.jxl, "bench requires an MJXL AVI");
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
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
