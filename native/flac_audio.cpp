#include "flac_audio.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

namespace mjxl {
namespace {
void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct ContextDeleter {
    void operator()(AVCodecContext* context) const
    {
        avcodec_free_context(&context);
    }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};
struct PacketDeleter {
    void operator()(AVPacket* packet) const
    {
        av_packet_free(&packet);
    }
};
using Context = std::unique_ptr<AVCodecContext, ContextDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;

Frame make_frame()
{
    Frame frame(av_frame_alloc());
    require(frame != nullptr, "Cannot allocate audio frame");
    return frame;
}

Packet make_packet()
{
    Packet packet(av_packet_alloc());
    require(packet != nullptr, "Cannot allocate audio packet");
    return packet;
}

// Convert PCM samples to the left-aligned integers accepted by the FFmpeg FLAC encoder.
void write_samples(uint8_t* target, const uint8_t* source, size_t samples, unsigned bits)
{
    if (bits == 8) {
        auto output = reinterpret_cast<int16_t*>(target);
        for (size_t i = 0; i < samples; ++i) {
            output[i] = static_cast<int16_t>((int(source[i]) - 128) * 256);
        }
    } else if (bits == 16) {
        std::memcpy(target, source, samples * 2);
    } else {
        auto output = reinterpret_cast<int32_t*>(target);
        for (size_t i = 0; i < samples; ++i) {
            const uint32_t value = uint32_t(source[i * 3]) << 8 | uint32_t(source[i * 3 + 1]) << 16 | uint32_t(source[i * 3 + 2]) << 24;
            output[i] = static_cast<int32_t>(value);
        }
    }
}

// Restore decoded samples to original PCM bytes; reject values outside the original bit depth.
void append_sample(std::vector<uint8_t>& output, int32_t value, unsigned bits)
{
    if (bits == 8) {
        require((value & 0xff) == 0 && value >= -32768 && value <= 32767, "FLAC sample does not fit original PCM8");
        output.push_back(static_cast<uint8_t>((value >> 8) + 128));
    } else if (bits == 16) {
        require(value >= -32768 && value <= 32767, "FLAC sample does not fit original PCM16");
        output.push_back(static_cast<uint8_t>(value & 0xff));
        output.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    } else {
        require((value & 0xff) == 0, "FLAC sample does not fit original PCM24");
        output.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        output.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        output.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    }
}

int32_t read_sample(const AVFrame& frame, int channel, int sample, int channels)
{
    const auto format = static_cast<AVSampleFormat>(frame.format);
    if (format == AV_SAMPLE_FMT_S16) {
        return reinterpret_cast<const int16_t*>(frame.extended_data[0])[size_t(sample) * channels + channel];
    }
    if (format == AV_SAMPLE_FMT_S16P) {
        return reinterpret_cast<const int16_t*>(frame.extended_data[channel])[sample];
    }
    if (format == AV_SAMPLE_FMT_S32) {
        return reinterpret_cast<const int32_t*>(frame.extended_data[0])[size_t(sample) * channels + channel];
    }
    if (format == AV_SAMPLE_FMT_S32P) {
        return reinterpret_cast<const int32_t*>(frame.extended_data[channel])[sample];
    }
    throw std::runtime_error("Unexpected FLAC decoder sample format");
}
}

bool flac_supported(const PcmFormat& format)
{
    return (format.bits == 8 || format.bits == 16 || format.bits == 24) && format.channels >= 1 && format.channels <= 8 && format.sampleRate > 0;
}

FlacStream flac_encode(std::span<const uint8_t> pcm, const PcmFormat& format)
{
    require(flac_supported(format), "PCM layout cannot be stored as FLAC");
    const size_t sampleBytes = format.bits / 8;
    const size_t blockAlign = sampleBytes * format.channels;
    require(!pcm.empty() && pcm.size() % blockAlign == 0, "PCM data is not sample aligned");
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
    require(codec != nullptr, "FLAC encoder is unavailable");
    Context context(avcodec_alloc_context3(codec));
    require(context != nullptr, "Cannot allocate FLAC encoder");
    FlacStream stream;
    stream.storedBits = format.bits == 24 ? 24 : 16;
    context->sample_fmt = format.bits == 24 ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
    context->bits_per_raw_sample = static_cast<int>(stream.storedBits);
    context->sample_rate = static_cast<int>(format.sampleRate);
    context->time_base = {1, static_cast<int>(format.sampleRate)};
    context->compression_level = 12;
    // At low sample rates, FFmpeg defaults to small blocks with high header and AVI overhead; use roughly 0.25-second blocks.
    context->frame_size = format.sampleRate <= 16000 ? 2048 : 4096;
    // The FFmpeg FLAC encoder requires a predefined layout for each channel count; sample order is preserved.
    const AVChannelLayout layouts[] = {AV_CHANNEL_LAYOUT_MONO, AV_CHANNEL_LAYOUT_STEREO, AV_CHANNEL_LAYOUT_SURROUND, AV_CHANNEL_LAYOUT_QUAD,
                                       AV_CHANNEL_LAYOUT_5POINT0_BACK, AV_CHANNEL_LAYOUT_5POINT1_BACK, AV_CHANNEL_LAYOUT_6POINT1, AV_CHANNEL_LAYOUT_7POINT1};
    require(av_channel_layout_copy(&context->ch_layout, &layouts[format.channels - 1]) >= 0, "Cannot set audio channels");
    require(avcodec_open2(context.get(), codec, nullptr) >= 0, "Cannot open FLAC encoder");
    require(context->frame_size > 0, "FLAC encoder reported no frame size");
    stream.frameSamples = static_cast<unsigned>(context->frame_size);
    stream.streamInfo.assign(context->extradata, context->extradata + context->extradata_size);

    auto packet = make_packet();
    const auto collect = [&]() {
        for (;;) {
            const int status = avcodec_receive_packet(context.get(), packet.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return;
            }
            require(status >= 0, "FLAC encoding failed");
            // The final encoder packet may contain only STREAMINFO without samples.
            if (packet->size > 0) {
                stream.packets.push_back({packet->pts, {packet->data, packet->data + packet->size}});
            }
            // STREAMINFO in the final packet contains the total length and MD5.
            size_t infoSize = 0;
            const uint8_t* info = av_packet_get_side_data(packet.get(), AV_PKT_DATA_NEW_EXTRADATA, &infoSize);
            if (info && infoSize) {
                stream.streamInfo.assign(info, info + infoSize);
            }
            av_packet_unref(packet.get());
        }
    };

    const size_t totalSamples = pcm.size() / blockAlign;
    for (size_t position = 0; position < totalSamples;) {
        const size_t count = std::min<size_t>(totalSamples - position, stream.frameSamples);
        auto frame = make_frame();
        frame->format = context->sample_fmt;
        frame->nb_samples = static_cast<int>(count);
        frame->sample_rate = context->sample_rate;
        frame->pts = static_cast<int64_t>(position);
        require(av_channel_layout_copy(&frame->ch_layout, &context->ch_layout) >= 0, "Cannot set audio channels");
        require(av_frame_get_buffer(frame.get(), 0) >= 0, "Cannot allocate audio samples");
        write_samples(frame->data[0], pcm.data() + position * blockAlign, count * format.channels, format.bits);
        require(avcodec_send_frame(context.get(), frame.get()) >= 0, "Cannot send PCM to FLAC encoder");
        collect();
        position += count;
    }
    require(avcodec_send_frame(context.get(), nullptr) >= 0, "Cannot flush FLAC encoder");
    collect();
    require(!stream.packets.empty() && stream.streamInfo.size() == 34, "FLAC encoder produced no complete stream");
    return stream;
}

struct FlacDecoder::State {
    Context context;
    Frame frame = make_frame();
    Packet packet = make_packet();
    PcmFormat format;

    // Convert every buffered decoder frame back to the original PCM bytes.
    std::vector<uint8_t> drain()
    {
        std::vector<uint8_t> output;
        for (;;) {
            const int status = avcodec_receive_frame(context.get(), frame.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return output;
            }
            require(status >= 0, "FLAC decoding failed");
            const int channels = frame->ch_layout.nb_channels;
            require(channels == static_cast<int>(format.channels) && frame->sample_rate == static_cast<int>(format.sampleRate), "FLAC stream format differs from AVI header");
            output.reserve(output.size() + size_t(frame->nb_samples) * channels * (format.bits / 8));
            for (int sample = 0; sample < frame->nb_samples; ++sample) {
                for (int channel = 0; channel < channels; ++channel) {
                    append_sample(output, read_sample(*frame, channel, sample, channels), format.bits);
                }
            }
            av_frame_unref(frame.get());
        }
    }
};

FlacDecoder::FlacDecoder(std::span<const uint8_t> streamInfo, const PcmFormat& format) : state_(std::make_unique<State>())
{
    require(flac_supported(format), "PCM layout cannot be restored from FLAC");
    require(streamInfo.size() == 34, "Invalid FLAC STREAMINFO");
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_FLAC);
    require(codec != nullptr, "FLAC decoder is unavailable");
    state_->context.reset(avcodec_alloc_context3(codec));
    require(state_->context != nullptr, "Cannot allocate FLAC decoder");
    state_->format = format;
    auto& context = *state_->context;
    context.extradata = static_cast<uint8_t*>(av_mallocz(streamInfo.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    require(context.extradata != nullptr, "Cannot allocate FLAC STREAMINFO");
    std::memcpy(context.extradata, streamInfo.data(), streamInfo.size());
    context.extradata_size = static_cast<int>(streamInfo.size());
    require(avcodec_open2(&context, codec, nullptr) >= 0, "Cannot open FLAC decoder");
}

FlacDecoder::~FlacDecoder() = default;

std::vector<uint8_t> FlacDecoder::decode(std::span<const uint8_t> packet)
{
    require(!packet.empty() && av_new_packet(state_->packet.get(), static_cast<int>(packet.size())) >= 0, "Cannot allocate FLAC packet");
    std::memcpy(state_->packet->data, packet.data(), packet.size());
    const int status = avcodec_send_packet(state_->context.get(), state_->packet.get());
    av_packet_unref(state_->packet.get());
    require(status >= 0, "Cannot send FLAC frame to decoder");
    return state_->drain();
}

std::vector<uint8_t> FlacDecoder::flush()
{
    require(avcodec_send_packet(state_->context.get(), nullptr) >= 0, "Cannot flush FLAC decoder");
    return state_->drain();
}
}
