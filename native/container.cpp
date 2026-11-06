#include "container.hpp"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
}

namespace fs = std::filesystem;
namespace mjxl {
constexpr uint16_t kFlacFormatTag = 0xF1AC;
constexpr uint32_t kAudioMetadataVersion = 1;
constexpr auto kOriginalAttachment = "mjxl-original.bin";
constexpr auto kOriginalMime = "application/x-mjxl-original";

static AVRational to_av(Rational value)
{
    return {value.num, value.den};
}

static bool original_attachment(const AVStream* stream)
{
    const auto name = av_dict_get(stream->metadata, "filename", nullptr, 0);
    const auto mime = av_dict_get(stream->metadata, "mimetype", nullptr, 0);
    return stream->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT && name && mime &&
           std::strcmp(name->value, kOriginalAttachment) == 0 && std::strcmp(mime->value, kOriginalMime) == 0;
}
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

// hdrl 내부 mjxa에서 원본 PCM 비트 깊이를 읽는다.
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

Movie read_movie(const fs::path& path)
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
            check(info->codec_id == AV_CODEC_ID_MJPEG || info->codec_tag == MJXL, "Input must be MJPEG or experimental MJXL");
            video = static_cast<int>(i);
            movie.jxl = info->codec_tag == MJXL;
            movie.width = info->width;
            movie.height = info->height;
            const auto fps = av_guess_frame_rate(raw, stream, nullptr);
            movie.fps = {fps.num, fps.den};
            check(movie.fps.num > 0 && movie.fps.den > 0, "Cannot determine constant frame rate");
        } else if (info->codec_type == AVMEDIA_TYPE_AUDIO) {
            check(audio < 0, "Multiple audio streams are not supported");
            audio = static_cast<int>(i);
            movie.sampleRate = info->sample_rate;
            movie.channels = info->ch_layout.nb_channels;
            if (info->codec_id == AV_CODEC_ID_FLAC) {
                movie.flac = true;
                const auto bits = av_dict_get(stream->metadata, "MJXL_ORIGINAL_BITS", nullptr, 0);
                if (bits) {
                    const std::string value(bits->value);
                    check(value == "8" || value == "16" || value == "24", "Invalid MJXL audio metadata");
                    movie.bits = static_cast<unsigned>(std::stoi(value));
                } else {
                    movie.bits = read_original_bits(path);
                }
                check(info->extradata && info->extradata_size == 34, "FLAC audio has no STREAMINFO");
                movie.streamInfo.assign(info->extradata, info->extradata + info->extradata_size);
                check(mjxl::flac_supported({movie.bits, movie.channels, movie.sampleRate}), "Invalid MJXL audio metadata");
            } else {
                check(info->codec_id == AV_CODEC_ID_PCM_U8 || info->codec_id == AV_CODEC_ID_PCM_S16LE || info->codec_id == AV_CODEC_ID_PCM_S24LE || info->codec_id == AV_CODEC_ID_PCM_S32LE, "Audio must be FLAC, unsigned PCM8 or signed little-endian PCM16/24/32");
                movie.bits = av_get_bits_per_sample(info->codec_id);
            }
            check(movie.sampleRate > 0 && movie.channels > 0 && movie.channels <= 8, "Invalid audio format");
        } else if (original_attachment(stream)) {
            continue;
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
                        const auto expected = av_rescale_q(static_cast<int64_t>(frame), av_inv_q(to_av(movie.fps)), raw->streams[video]->time_base);
                        check(std::abs(p->pts - expected) <= 1, "Variable frame rate is not supported");
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

class AviWriter final : public ContainerWriter {
    std::fstream file_;
    uint64_t movi_ = 0, frameCountPos_ = 0, videoLengthPos_ = 0, audioLengthPos_ = 0;
    uint32_t frames_ = 0, samples_ = 0, blockAlign_ = 0;
    bool flac_ = false;
    struct Entry { uint32_t id, flags, offset, size; };
    std::vector<Entry> index_;

    // FLAC 프레임을 가변 크기 청크로 선언하고 원본 비트 깊이는 별도로 보존한다.
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
        // AVI Splitter가 청크 길이를 계산하도록 VBR 규칙의 프레임당 샘플 수를 기록한다.
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
            // cbSize가 0인 18바이트 형식의 PotPlayer 잡음을 피하도록 16바이트 PCMWAVEFORMAT을 쓴다.
            tag("strf"); u32(16); u16(1); u16(static_cast<uint16_t>(movie.channels));
            u32(movie.sampleRate); u32(movie.sampleRate * blockAlign_);
            u16(static_cast<uint16_t>(blockAlign_)); u16(static_cast<uint16_t>(movie.bits));
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
    void packet(bool video, std::span<const uint8_t> data) override
    {
        const auto offset = position() - (movi_ + 4);
        const uint32_t id = video ? fourcc('0', '0', 'd', 'c') : fourcc('0', '1', 'w', 'b');
        u32(id); u32(static_cast<uint32_t>(data.size())); bytes(data.data(), data.size());
        if (data.size() & 1) { const char zero = 0; bytes(&zero, 1); }
        index_.push_back({id, 0x10, static_cast<uint32_t>(offset), static_cast<uint32_t>(data.size())});
        if (video) {
            ++frames_;
        } else if (flac_) {
            // 가변 크기 오디오 스트림의 길이는 청크 수로 기록한다.
            ++samples_;
        } else {
            check(blockAlign_ && data.size() % blockAlign_ == 0, "PCM packet is not sample aligned");
            samples_ += static_cast<uint32_t>(data.size() / blockAlign_);
        }
    }
    void finish(std::span<const uint8_t> original = {}) override
    {
        finish_list(movi_);
        tag("idx1"); u32(static_cast<uint32_t>(index_.size() * sizeof(Entry)));
        bytes(index_.data(), index_.size() * sizeof(Entry));
        if (!original.empty()) {
            // 플레이어가 건너뛸 수 있는 후행 RIFF 청크에 원본 복원 데이터를 보존한다.
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

// 최상위 mjxo 청크 또는 MKV attachment를 읽고 없으면 빈 값을 반환한다.
std::vector<uint8_t> read_original_chunk(const fs::path& path)
{
    const auto total = fs::file_size(path);
    std::ifstream file(path, std::ios::binary);
    char header[12]{};
    file.read(header, sizeof(header));
    check(bool(file), "Cannot read container header");
    if (std::memcmp(header, "RIFF", 4) != 0) {
        AVFormatContext* raw = nullptr;
        check(avformat_open_input(&raw, utf8(path).c_str(), nullptr, nullptr) >= 0, "Cannot open recovery container");
        auto closer = [](AVFormatContext* ptr) { avformat_close_input(&ptr); };
        std::unique_ptr<AVFormatContext, decltype(closer)> input(raw, closer);
        std::vector<uint8_t> result;
        bool found = false;
        for (unsigned i = 0; i < raw->nb_streams; ++i) {
            if (original_attachment(raw->streams[i])) {
                check(!found, "Duplicate original-file attachments");
                found = true;
                const auto info = raw->streams[i]->codecpar;
                check(info->extradata && info->extradata_size > 0, "Empty original-file attachment");
                result.assign(info->extradata, info->extradata + info->extradata_size);
            }
        }
        return result;
    }
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

static void check_av(int result, const char* operation)
{
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}

class MatroskaWriter final : public ContainerWriter {
    static void close(AVFormatContext* context)
    {
        if (context) {
            if (context->pb) {
                avio_closep(&context->pb);
            }
            avformat_free_context(context);
        }
    }
    std::unique_ptr<AVFormatContext, decltype(&close)> context_{nullptr, close};
    AVStream* video_ = nullptr;
    AVStream* audio_ = nullptr;
    AVRational videoTimeBase_{};
    unsigned sampleRate_ = 0, blockAlign_ = 0, frameSamples_ = 0;
    int64_t frames_ = 0, samples_ = 0, totalSamples_ = 0;

    static void extradata(AVCodecParameters* parameters, std::span<const uint8_t> data)
    {
        check(data.size() < INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE, "Container metadata is too large");
        parameters->extradata = static_cast<uint8_t*>(av_mallocz(data.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        check(parameters->extradata != nullptr, "Cannot allocate container metadata");
        std::memcpy(parameters->extradata, data.data(), data.size());
        parameters->extradata_size = static_cast<int>(data.size());
    }
public:
    MatroskaWriter(const fs::path& path, const Movie& movie, const mjxl::FlacStream* flac,
                   std::span<const uint8_t> original)
    {
        check(!fs::exists(path), "Output or partial output already exists");
        AVFormatContext* raw = nullptr;
        check_av(avformat_alloc_output_context2(&raw, nullptr, "matroska", utf8(path).c_str()), "Create Matroska muxer");
        context_.reset(raw);
        video_ = avformat_new_stream(raw, nullptr);
        check(video_ != nullptr, "Cannot allocate video stream");
        videoTimeBase_ = av_inv_q(to_av(movie.fps));
        video_->time_base = videoTimeBase_;
        video_->avg_frame_rate = to_av(movie.fps);
        video_->r_frame_rate = to_av(movie.fps);
        auto video = video_->codecpar;
        video->codec_type = AVMEDIA_TYPE_VIDEO;
        video->codec_id = AV_CODEC_ID_NONE;
        video->codec_tag = MJXL;
        video->width = movie.width;
        video->height = movie.height;
        video->bits_per_coded_sample = 24;
        if (movie.channels) {
            audio_ = avformat_new_stream(raw, nullptr);
            check(audio_ != nullptr, "Cannot allocate audio stream");
            sampleRate_ = movie.sampleRate;
            blockAlign_ = movie.channels * movie.bits / 8;
            uint64_t audioBytes = 0;
            for (const auto& packet : movie.packets) {
                if (!packet.video) {
                    audioBytes += packet.bytes.size();
                }
            }
            check(audioBytes % blockAlign_ == 0, "PCM input is not sample aligned");
            totalSamples_ = static_cast<int64_t>(audioBytes / blockAlign_);
            audio_->time_base = {1, static_cast<int>(sampleRate_)};
            auto audio = audio_->codecpar;
            audio->codec_type = AVMEDIA_TYPE_AUDIO;
            audio->sample_rate = sampleRate_;
            av_channel_layout_default(&audio->ch_layout, movie.channels);
            audio->bits_per_coded_sample = flac ? flac->storedBits : movie.bits;
            audio->bits_per_raw_sample = audio->bits_per_coded_sample;
            if (flac) {
                audio->codec_id = AV_CODEC_ID_FLAC;
                frameSamples_ = flac->frameSamples;
                extradata(audio, flac->streamInfo);
                check_av(av_dict_set(&audio_->metadata, "MJXL_ORIGINAL_BITS", std::to_string(movie.bits).c_str(), 0), "Set original PCM depth");
            } else {
                switch (movie.bits) {
                case 8: audio->codec_id = AV_CODEC_ID_PCM_U8; break;
                case 16: audio->codec_id = AV_CODEC_ID_PCM_S16LE; break;
                case 24: audio->codec_id = AV_CODEC_ID_PCM_S24LE; break;
                case 32: audio->codec_id = AV_CODEC_ID_PCM_S32LE; break;
                default: throw std::runtime_error("Unsupported PCM depth");
                }
                audio->block_align = blockAlign_;
                audio->bit_rate = int64_t(sampleRate_) * blockAlign_ * 8;
            }
        }
        if (!original.empty()) {
            const auto attachment = avformat_new_stream(raw, nullptr);
            check(attachment != nullptr, "Cannot allocate original-file attachment");
            attachment->codecpar->codec_type = AVMEDIA_TYPE_ATTACHMENT;
            extradata(attachment->codecpar, original);
            check_av(av_dict_set(&attachment->metadata, "filename", kOriginalAttachment, 0), "Set attachment filename");
            check_av(av_dict_set(&attachment->metadata, "mimetype", kOriginalMime, 0), "Set attachment type");
        }
        check_av(avio_open(&raw->pb, utf8(path).c_str(), AVIO_FLAG_WRITE), "Open Matroska output");
        check_av(avformat_write_header(raw, nullptr), "Write Matroska header");
    }

    void packet(bool video, std::span<const uint8_t> data) override
    {
        const auto stream = video ? video_ : audio_;
        check(stream != nullptr && data.size() < INT_MAX, "Invalid Matroska packet");
        AVPacket* raw = av_packet_alloc();
        check(raw != nullptr, "Cannot allocate Matroska packet");
        auto closer = [](AVPacket* value) { av_packet_free(&value); };
        std::unique_ptr<AVPacket, decltype(closer)> packet(raw, closer);
        check_av(av_new_packet(raw, static_cast<int>(data.size())), "Allocate packet bytes");
        std::memcpy(raw->data, data.data(), data.size());
        raw->stream_index = stream->index;
        raw->flags = AV_PKT_FLAG_KEY;
        const auto timeBase = video ? videoTimeBase_ : AVRational{1, static_cast<int>(sampleRate_)};
        auto& position = video ? frames_ : samples_;
        if (!video && !frameSamples_) {
            check(data.size() % blockAlign_ == 0, "PCM packet is not sample aligned");
        }
        const int64_t duration = video ? 1 : frameSamples_ ?
            (std::min<int64_t>)(frameSamples_, totalSamples_ - samples_) : static_cast<int64_t>(data.size() / blockAlign_);
        check(duration > 0, "Invalid Matroska packet duration");
        raw->pts = raw->dts = av_rescale_q(position, timeBase, stream->time_base);
        raw->duration = av_rescale_q(position + duration, timeBase, stream->time_base) - raw->pts;
        position += duration;
        check_av(av_interleaved_write_frame(context_.get(), raw), "Write Matroska packet");
    }

    void finish(std::span<const uint8_t> = {}) override
    {
        check_av(av_write_trailer(context_.get()), "Finish Matroska output");
        check_av(avio_closep(&context_->pb), "Close Matroska output");
    }
};

std::unique_ptr<ContainerWriter> make_avi_writer(const fs::path& path, const Movie& movie, uint32_t videoCodec, const FlacStream* flac)
{
    return std::make_unique<AviWriter>(path, movie, videoCodec, flac);
}

std::unique_ptr<ContainerWriter> make_matroska_writer(const fs::path& path, const Movie& movie, const FlacStream* flac,
                                                       std::span<const uint8_t> original)
{
    return std::make_unique<MatroskaWriter>(path, movie, flac, original);
}

void configure_container_logging()
{
    av_log_set_level(AV_LOG_ERROR);
}
}
