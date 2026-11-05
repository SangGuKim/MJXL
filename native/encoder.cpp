#include "cli.hpp"
#include "core.hpp"
#include "flac_audio.hpp"
#include "original_file.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <chrono>
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
constexpr uint16_t kFlacFormatTag = 0xF1AC;
constexpr uint32_t kAudioMetadataVersion = 1;
struct Packet {
    bool video;
    std::vector<uint8_t> bytes;
    int64_t position;
};
struct Movie {
    // FLAC AVI에서도 bits는 저장 비트가 아니라 복원할 원래 PCM 비트이다.
    unsigned width = 0, height = 0, sampleRate = 0, channels = 0, bits = 0;
    AVRational fps{};
    bool jxl = false;
    bool flac = false;
    std::vector<uint8_t> streamInfo;
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

// hdrl 안의 mjxa 청크에서 FLAC로 저장하기 전의 PCM 비트 수를 읽는다.
static unsigned read_original_bits(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    char header[12]{};
    file.read(header, sizeof(header));
    check(file && std::memcmp(header, "RIFF", 4) == 0 && std::memcmp(header + 8, "AVI ", 4) == 0, "FLAC audio is only supported in AVI");
    const auto next = [&](char* tag, uint32_t& size) {
        file.read(tag, 4);
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        return bool(file);
    };
    char tag[4]{};
    uint32_t size = 0;
    while (next(tag, size)) {
        char type[4]{};
        if (std::memcmp(tag, "LIST", 4) == 0) {
            check(size >= 4 && bool(file.read(type, 4)), "Invalid AVI list");
            if (std::memcmp(type, "hdrl", 4) == 0) {
                const auto end = static_cast<std::streamoff>(file.tellg()) + size - 4;
                while (static_cast<std::streamoff>(file.tellg()) < end && next(tag, size)) {
                    if (std::memcmp(tag, "mjxa", 4) == 0) {
                        uint32_t values[2]{};
                        check(size == sizeof(values) && bool(file.read(reinterpret_cast<char*>(values), sizeof(values))), "Invalid MJXL audio metadata");
                        check(values[0] == kAudioMetadataVersion, "Unknown MJXL audio metadata version");
                        return values[1];
                    }
                    file.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
                }
                break;
            }
            file.seekg(std::streamoff(size) - 4 + (size & 1), std::ios::cur);
        } else {
            file.seekg(std::streamoff(size) + (size & 1), std::ios::cur);
        }
    }
    throw std::runtime_error("FLAC audio without MJXL metadata is unsupported");
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
            audio = static_cast<int>(i);
            movie.sampleRate = info->sample_rate;
            movie.channels = info->ch_layout.nb_channels;
            if (info->codec_id == AV_CODEC_ID_FLAC) {
                movie.flac = true;
                movie.bits = read_original_bits(path);
                check(info->extradata && info->extradata_size == 34, "FLAC audio has no STREAMINFO");
                movie.streamInfo.assign(info->extradata, info->extradata + info->extradata_size);
                check(mjxl::flac_supported({movie.bits, movie.channels, movie.sampleRate}), "Invalid MJXL audio metadata");
            } else {
                check(info->codec_id == AV_CODEC_ID_PCM_U8 || info->codec_id == AV_CODEC_ID_PCM_S16LE || info->codec_id == AV_CODEC_ID_PCM_S24LE || info->codec_id == AV_CODEC_ID_PCM_S32LE, "Audio must be FLAC, unsigned PCM8 or signed little-endian PCM16/24/32");
                movie.bits = av_get_bits_per_sample(info->codec_id);
            }
            check(movie.sampleRate > 0 && movie.channels > 0 && movie.channels <= 8, "Invalid audio format");
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
                movie.packets.push_back({isVideo, {p->data, p->data + p->size}, p->pos});
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
    bool flac_ = false;
    struct Entry { uint32_t id, flags, offset, size; };
    std::vector<Entry> index_;

    // FLAC 프레임을 가변 크기 오디오 청크로 선언하고 원래 PCM 비트를 별도 청크에 남긴다.
    void write_flac_header(const Movie& movie, const mjxl::FlacStream& flac)
    {
        uint64_t total = 0;
        uint32_t largest = 0;
        for (const auto& packet : flac.packets) {
            total += packet.bytes.size();
            largest = (std::max)(largest, static_cast<uint32_t>(packet.bytes.size()));
        }
        const uint64_t last = flac.packets.back().position;
        const uint64_t samples = std::max<uint64_t>(last + flac.frameSamples, 1);
        const auto stream = list("strl");
        tag("strh"); u32(56); tag("auds"); u32(0); u32(0); u16(0); u16(0); u32(0);
        u32(flac.frameSamples); u32(movie.sampleRate); u32(0);
        audioLengthPos_ = position(); u32(0);
        u32(largest); u32(0xffffffff); u32(0);
        for (int i = 0; i < 4; ++i) { u16(0); }
        tag("strf"); u32(static_cast<uint32_t>(18 + flac.streamInfo.size()));
        u16(kFlacFormatTag); u16(static_cast<uint16_t>(movie.channels));
        u32(movie.sampleRate); u32(static_cast<uint32_t>(total * movie.sampleRate / samples));
        // AVI Splitter가 청크당 재생 시간을 계산할 수 있게 VBR 관례대로 프레임당 샘플 수를 적는다.
        u16(static_cast<uint16_t>(flac.frameSamples)); u16(static_cast<uint16_t>(flac.storedBits));
        u16(static_cast<uint16_t>(flac.streamInfo.size()));
        bytes(flac.streamInfo.data(), flac.streamInfo.size());
        finish_list(stream);
        tag("mjxa"); u32(8); u32(kAudioMetadataVersion); u32(movie.bits);
    }
public:
    explicit AviWriter(const fs::path& path, const Movie& movie, uint32_t videoCodec = MJXL, const mjxl::FlacStream* flac = nullptr)
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
        if (flac) {
            flac_ = true;
            write_flac_header(movie, *flac);
        } else if (movie.channels) {
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
        } else if (flac_) {
            // 가변 크기 오디오 스트림의 길이는 청크 수이다.
            ++samples_;
        } else {
            check(blockAlign_ && data.size() % blockAlign_ == 0, "PCM packet is not sample aligned");
            samples_ += static_cast<uint32_t>(data.size() / blockAlign_);
        }
    }
    void finish(std::span<const uint8_t> original = {})
    {
        finish_list(movi_);
        tag("idx1"); u32(static_cast<uint32_t>(index_.size() * sizeof(Entry)));
        bytes(index_.data(), index_.size() * sizeof(Entry));
        if (!original.empty()) {
            // 재생기가 건너뛰도록 원본 복원 정보를 RIFF 끝의 별도 청크에 둔다.
            tag("mjxo"); u32(static_cast<uint32_t>(original.size()));
            bytes(original.data(), original.size());
            if (original.size() & 1) { const char zero = 0; bytes(&zero, 1); }
        }
        patch(frameCountPos_, frames_); patch(videoLengthPos_, frames_);
        if (audioLengthPos_) { patch(audioLengthPos_, samples_); }
        patch(4, static_cast<uint32_t>(position() - 8));
        file_.close();
    }
};

// 저장 형식과 상관없이 음성 전체를 원래 PCM 바이트로 돌려준다.
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
    check(target.jxl == expectedJxl && target.width == source.width && target.height == source.height && av_cmp_q(target.fps, source.fps) == 0, "Video format verification failed");
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

// RIFF 최상위에서 mjxo 청크를 찾는다. 없으면 빈 값을 돌려준다.
static std::vector<uint8_t> read_original_chunk(const fs::path& path)
{
    const auto total = fs::file_size(path);
    std::ifstream file(path, std::ios::binary);
    char header[12]{};
    file.read(header, sizeof(header));
    check(file && std::memcmp(header, "RIFF", 4) == 0, "Original data is only supported in AVI");
    uint64_t position = sizeof(header);
    while (position + 8 <= total) {
        char tag[4]{};
        uint32_t size = 0;
        file.seekg(static_cast<std::streamoff>(position));
        file.read(tag, 4);
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        check(bool(file), "Cannot read AVI chunk");
        if (std::memcmp(tag, "mjxo", 4) == 0) {
            check(size <= total - position - 8, "Truncated original data chunk");
            std::vector<uint8_t> payload(size);
            file.read(reinterpret_cast<char*>(payload.data()), size);
            check(bool(file), "Cannot read original data chunk");
            return payload;
        }
        position += 8 + uint64_t(size) + (size & 1);
    }
    return {};
}

// 원본을 복원할 수 없는 출력을 조용히 만들지 않도록 복원 정보를 만들지 못하면 인코딩을 중단한다.
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

// 인코딩된 AVI만으로 원본 파일 전체를 다시 만든다.
static std::vector<uint8_t> rebuild_from_avi(const Movie& encoded, std::span<const uint8_t> payload, const mjxl::Codec& codec)
{
    std::vector<std::vector<uint8_t>> frames;
    for (const auto& p : encoded.packets) {
        if (p.video) { frames.push_back(codec.jpeg(p.bytes)); }
    }
    return mjxl::rebuild_original(payload, frames, pcm_of(encoded));
}

// 내부 문자열은 UTF-8이므로 경로로 바꿀 때 플랫폼 인코딩에 맡기지 않는다.
static fs::path to_path(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

static int run(const std::vector<std::string>& arguments)
{
    av_log_set_level(AV_LOG_ERROR);
    try {
        const auto cli = mjxl::parse_command_line(arguments);
        if (cli.help) {
            std::cout << mjxl::help_text(cli.command);
            return cli.missingCommand ? 1 : 0;
        }
        const auto& command = cli.command;
        const bool wantFlac = cli.flac, noOriginal = cli.noOriginal;
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
            auto partial = output; partial += ".partial";
            const mjxl::PcmFormat format{movie.bits, movie.channels, movie.sampleRate};
            const auto pcm = pcm_of(movie);
            std::optional<mjxl::FlacStream> flac;
            if (wantFlac && !pcm.empty() && mjxl::flac_supported(format)) {
                flac = mjxl::flac_encode(pcm, format);
                // 파일을 쓰기 전에 FLAC가 원래 PCM으로 되돌아오는지 확인한다.
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
            } else if (wantFlac && !pcm.empty()) {
                std::cout << "Audio stays PCM because FLAC cannot store it: " << pcm.size() << " bytes\n";
            }
            const auto sourceFile = read_file(firstPath);
            const auto originalData = keepOriginal ? make_original_data(movie, sourceFile) : std::vector<uint8_t>{};
            if (!keepOriginal) {
                std::cout << "WARNING: This output cannot restore the original file; keep the original\n";
            }
            AviWriter writer(partial, movie, MJXL, flac ? &*flac : nullptr);
            unsigned frames = 0;
            size_t nextAudio = 0;
            // FLAC 프레임은 재생 시각이 해당 영상 프레임에 도달하기 전에 기록한다.
            const auto write_audio_until = [&](int64_t frame) {
                while (flac && nextAudio < flac->packets.size() &&
                       flac->packets[nextAudio].position * movie.fps.num <= frame * movie.fps.den * movie.sampleRate) {
                    writer.packet(false, flac->packets[nextAudio].bytes);
                    ++nextAudio;
                }
            };
            for (const auto& p : movie.packets) {
                if (p.video) {
                    write_audio_until(frames);
                    const auto encoded = codec.encode(p.bytes, effort);
                    check(codec.jpeg(encoded) == p.bytes, "JPEG round-trip verification failed");
                    writer.packet(true, encoded);
                    ++frames;
                    if (frames % 60 == 0) { std::cout << "Encoded " << frames << " frames\n"; }
                } else if (!flac) {
                    writer.packet(false, p.bytes);
                }
            }
            if (flac) {
                // 마지막 영상 프레임 뒤에 남은 음성을 모두 기록한다.
                write_audio_until(INT64_MAX / (int64_t(movie.fps.den) * movie.sampleRate));
            }
            writer.finish(originalData);
            verify(movie, partial, codec);
            if (!originalData.empty()) {
                // 이름을 확정하기 전에 저장된 AVI만으로 원본 전체가 복원되는지 확인한다.
                const auto stored = read_original_chunk(partial);
                check(rebuild_from_avi(read_movie(partial), stored, codec) == sourceFile, "Original file verification failed");
                std::cout << "Verified exact original file: " << sourceFile.size() << " bytes, stored " << originalData.size() << " bytes of container data\n";
            }
            fs::rename(partial, output);
            const auto savedSize = fs::file_size(output);
            const double reduction = sourceFile.empty() ? 0.0 : 100.0 * (1.0 - static_cast<double>(savedSize) / static_cast<double>(sourceFile.size()));
            std::cout << "Saved " << savedSize << " bytes (" << std::format("{:.2f}", reduction) << "% smaller than " << sourceFile.size() << " bytes): " << utf8(output) << '\n';
        } else if (command == "restore") {
            check(movie.jxl, "restore requires an MJXL AVI input");
            const fs::path output(secondPath);
            check(!fs::exists(output), "Output already exists");
            auto partial = output; partial += ".partial";
            AviWriter writer(partial, movie, fourcc('M', 'J', 'P', 'G'));
            std::unique_ptr<mjxl::FlacDecoder> decoder;
            if (movie.flac) {
                decoder = std::make_unique<mjxl::FlacDecoder>(movie.streamInfo, mjxl::PcmFormat{movie.bits, movie.channels, movie.sampleRate});
            }
            for (const auto& p : movie.packets) {
                if (p.video) {
                    const auto jpeg = codec.jpeg(p.bytes);
                    writer.packet(true, jpeg);
                } else if (decoder) {
                    const auto pcm = decoder->decode(p.bytes);
                    if (!pcm.empty()) { writer.packet(false, pcm); }
                } else {
                    writer.packet(false, p.bytes);
                }
            }
            if (decoder) {
                const auto pcm = decoder->flush();
                if (!pcm.empty()) { writer.packet(false, pcm); }
            }
            writer.finish();
            verify(movie, partial, codec, false);
            fs::rename(partial, output);
            std::cout << "Restored exact JPEG packets and PCM samples to " << utf8(output) << '\n';
        } else if (command == "restore-original") {
            check(movie.jxl, "restore-original requires an MJXL AVI input");
            const fs::path output(secondPath);
            check(!fs::exists(output), "Output already exists");
            auto partial = output; partial += ".partial";
            check(!fs::exists(partial), "Partial output already exists");
            const auto stored = read_original_chunk(firstPath);
            check(!stored.empty(), "This AVI holds no original file data; encode it again from the original file");
            const auto rebuilt = rebuild_from_avi(movie, stored, codec);
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
                // 이 검사를 원본 삭제의 근거로 쓰므로 복원할 수 없는 파일은 실패로 처리한다.
                check(noOriginal, "This AVI holds no original file data, so the original file cannot be restored from it");
                std::cout << "WARNING: This AVI holds no original file data; keep the original\n";
            } else {
                check(rebuild_from_avi(read_movie(secondPath), stored, codec) == read_file(firstPath), "Original file verification failed");
                std::cout << "Verified exact original file\n";
            }
        } else if (command == "bench") {
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
    } catch (const mjxl::UsageError& error) {
        std::cerr << "Error: " << error.what() << "\nRun \"mjxl --help\" for usage.\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
// Windows 인자는 UTF-16으로 받아 UTF-8로 옮기고 콘솔도 UTF-8로 맞춘다.
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
