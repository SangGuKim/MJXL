#include "flac_audio.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <FLAC/stream_encoder.h>
#include <FLAC/stream_decoder.h>

namespace mjxl {
namespace {
void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct EncoderDelete {
    void operator()(FLAC__StreamEncoder* value) const
    {
        FLAC__stream_encoder_delete(value);
    }
};
struct DecoderDelete {
    void operator()(FLAC__StreamDecoder* value) const
    {
        FLAC__stream_decoder_delete(value);
    }
};
using Encoder = std::unique_ptr<FLAC__StreamEncoder, EncoderDelete>;
using Decoder = std::unique_ptr<FLAC__StreamDecoder, DecoderDelete>;

struct EncodeState {
    FlacStream stream;
    std::vector<uint8_t> header;
    std::exception_ptr error;
    uint64_t samples = 0;
    unsigned lastFrame = 0;
};

FLAC__StreamEncoderWriteStatus write_encoded(const FLAC__StreamEncoder*, const FLAC__byte data[], size_t bytes,
                                             unsigned samples, unsigned frame, void* opaque)
{
    auto& state = *static_cast<EncodeState*>(opaque);
    try {
        if (samples == 0) {
            state.header.insert(state.header.end(), data, data + bytes);
        } else {
            if (state.stream.packets.empty() || state.lastFrame != frame) {
                state.stream.packets.push_back({static_cast<int64_t>(state.samples), {}});
                state.samples += samples;
                state.lastFrame = frame;
            }
            auto& packet = state.stream.packets.back().bytes;
            packet.insert(packet.end(), data, data + bytes);
        }
        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    } catch (...) {
        state.error = std::current_exception();
        return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
    }
}

// 최종 STREAMINFO를 직접 직렬화해 seek 불가능한 메모리 출력에서도 총 샘플 수와 MD5를 보존한다.
void metadata_encoded(const FLAC__StreamEncoder*, const FLAC__StreamMetadata* metadata, void* opaque)
{
    auto& state = *static_cast<EncodeState*>(opaque);
    try {
        require(metadata->type == FLAC__METADATA_TYPE_STREAMINFO, "Unexpected FLAC metadata");
        const auto& info = metadata->data.stream_info;
        auto& bytes = state.stream.streamInfo;
        bytes.assign(34, 0);
        const auto put = [&](size_t offset, uint64_t value, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                bytes[offset + count - i - 1] = static_cast<uint8_t>(value >> (i * 8));
            }
        };
        put(0, info.min_blocksize, 2);
        put(2, info.max_blocksize, 2);
        put(4, info.min_framesize, 3);
        put(7, info.max_framesize, 3);
        const uint64_t packed = (uint64_t(info.sample_rate) << 44) | (uint64_t(info.channels - 1) << 41) |
                                (uint64_t(info.bits_per_sample - 1) << 36) | info.total_samples;
        put(10, packed, 8);
        std::copy_n(info.md5sum, 16, bytes.begin() + 18);
    } catch (...) {
        state.error = std::current_exception();
    }
}

int32_t pcm_sample(const uint8_t* data, unsigned bits)
{
    if (bits == 8) {
        return (int32_t(data[0]) - 128) * 256;
    }
    if (bits == 16) {
        const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8);
        return value & 0x8000 ? static_cast<int32_t>(value) - 0x10000 : static_cast<int32_t>(value);
    }
    const uint32_t value = uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16);
    return value & 0x800000 ? static_cast<int32_t>(value) - 0x1000000 : static_cast<int32_t>(value);
}

struct DecodeState {
    std::span<const uint8_t> input;
    size_t position = 0;
    PcmFormat format;
    unsigned storedBits = 0;
    std::vector<uint8_t> output;
    std::exception_ptr exception;
    bool error = false;
    unsigned frames = 0;
};

FLAC__StreamDecoderReadStatus read_encoded(const FLAC__StreamDecoder*, FLAC__byte target[], size_t* count, void* opaque)
{
    auto& state = *static_cast<DecodeState*>(opaque);
    if (*count == 0) {
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    *count = std::min(*count, state.input.size() - state.position);
    if (*count == 0) {
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }
    std::memcpy(target, state.input.data() + state.position, *count);
    state.position += *count;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus decoded(const FLAC__StreamDecoder*, const FLAC__Frame* frame,
                                       const FLAC__int32* const channels[], void* opaque)
{
    auto& state = *static_cast<DecodeState*>(opaque);
    try {
        require(frame->header.channels == state.format.channels && frame->header.sample_rate == state.format.sampleRate &&
                frame->header.bits_per_sample == state.storedBits, "FLAC frame differs from original audio format");
        ++state.frames;
        for (unsigned i = 0; i < frame->header.blocksize; ++i) {
            for (unsigned channel = 0; channel < state.format.channels; ++channel) {
                const int32_t sample = channels[channel][i];
                if (state.format.bits == 8) {
                    require((sample & 0xff) == 0 && sample >= -32768 && sample <= 32767,
                            "FLAC sample does not fit original PCM8");
                    state.output.push_back(static_cast<uint8_t>(sample / 256 + 128));
                } else {
                    const auto value = static_cast<uint32_t>(sample);
                    for (unsigned byte = 0; byte < state.format.bits / 8; ++byte) {
                        state.output.push_back(static_cast<uint8_t>(value >> (8 * byte)));
                    }
                }
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    } catch (...) {
        state.exception = std::current_exception();
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
}

void decode_error(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* opaque)
{
    static_cast<DecodeState*>(opaque)->error = true;
}
}

bool flac_supported(const PcmFormat& format)
{
    return (format.bits == 8 || format.bits == 16 || format.bits == 24) && format.channels >= 1 &&
           format.channels <= 8 && format.sampleRate > 0 && format.sampleRate <= 655350;
}

FlacStream flac_encode(std::span<const uint8_t> pcm, const PcmFormat& format)
{
    require(flac_supported(format), "PCM layout cannot be stored as FLAC");
    const size_t sampleBytes = format.bits / 8;
    const size_t blockAlign = sampleBytes * format.channels;
    require(!pcm.empty() && pcm.size() % blockAlign == 0, "PCM data is not sample aligned");
    const size_t totalSamples = pcm.size() / blockAlign;
    require(totalSamples < (uint64_t(1) << 36), "FLAC sample count is too large");
    Encoder encoder(FLAC__stream_encoder_new());
    require(encoder != nullptr, "Cannot allocate FLAC encoder");
    EncodeState state;
    state.stream.storedBits = format.bits == 24 ? 24 : 16;
    state.stream.frameSamples = format.sampleRate <= 16000 ? 2048 : 4096;
    require(FLAC__stream_encoder_set_streamable_subset(encoder.get(), false) &&
            FLAC__stream_encoder_set_channels(encoder.get(), format.channels) &&
            FLAC__stream_encoder_set_bits_per_sample(encoder.get(), state.stream.storedBits) &&
            FLAC__stream_encoder_set_sample_rate(encoder.get(), format.sampleRate) &&
            FLAC__stream_encoder_set_compression_level(encoder.get(), 8) &&
            FLAC__stream_encoder_set_blocksize(encoder.get(), state.stream.frameSamples) &&
            FLAC__stream_encoder_set_total_samples_estimate(encoder.get(), totalSamples), "Cannot configure FLAC encoder");
    const auto init = FLAC__stream_encoder_init_stream(encoder.get(), write_encoded, nullptr, nullptr, metadata_encoded, &state);
    if (state.error) {
        std::rethrow_exception(state.error);
    }
    require(init == FLAC__STREAM_ENCODER_INIT_STATUS_OK, "Cannot open FLAC encoder");
    std::vector<FLAC__int32> samples(size_t(state.stream.frameSamples) * format.channels);
    for (size_t position = 0; position < totalSamples;) {
        const auto count = static_cast<unsigned>(std::min<size_t>(state.stream.frameSamples, totalSamples - position));
        for (size_t i = 0; i < size_t(count) * format.channels; ++i) {
            samples[i] = pcm_sample(pcm.data() + position * blockAlign + i * sampleBytes, format.bits);
        }
        const bool ok = FLAC__stream_encoder_process_interleaved(encoder.get(), samples.data(), count);
        if (state.error) {
            std::rethrow_exception(state.error);
        }
        require(ok, "FLAC encoding failed");
        position += count;
    }
    const bool finished = FLAC__stream_encoder_finish(encoder.get());
    if (state.error) {
        std::rethrow_exception(state.error);
    }
    require(finished && state.samples == totalSamples && !state.stream.packets.empty() && state.stream.streamInfo.size() == 34,
            "FLAC encoder produced no complete stream");
    return std::move(state.stream);
}

struct FlacDecoder::State {
    std::vector<uint8_t> header;
    PcmFormat format;
    unsigned storedBits = 0;
};

FlacDecoder::FlacDecoder(std::span<const uint8_t> streamInfo, const PcmFormat& format) : state_(std::make_unique<State>())
{
    require(flac_supported(format) && streamInfo.size() == 34, "Invalid FLAC format or STREAMINFO");
    const unsigned bits = ((unsigned(streamInfo[12]) & 1) << 4 | (unsigned(streamInfo[13]) >> 4)) + 1;
    const unsigned channels = ((unsigned(streamInfo[12]) >> 1) & 7) + 1;
    const unsigned rate = unsigned(streamInfo[10]) << 12 | unsigned(streamInfo[11]) << 4 | unsigned(streamInfo[12]) >> 4;
    require(bits == (format.bits == 8 ? 16 : format.bits) && channels == format.channels && rate == format.sampleRate,
            "FLAC STREAMINFO differs from original PCM format");
    state_->storedBits = bits;
    state_->format = format;
    state_->header = {'f', 'L', 'a', 'C', 0x80, 0, 0, 34};
    state_->header.insert(state_->header.end(), streamInfo.begin(), streamInfo.end());
}

FlacDecoder::~FlacDecoder() = default;

std::vector<uint8_t> FlacDecoder::decode(std::span<const uint8_t> packet)
{
    require(!packet.empty(), "Empty FLAC packet");
    // 각 컨테이너 패킷을 독립 스트림으로 읽어 디코더의 다음 패킷 선행 읽기를 방지한다.
    auto input = state_->header;
    input.insert(input.end(), packet.begin(), packet.end());
    DecodeState state;
    state.input = input;
    state.format = state_->format;
    state.storedBits = state_->storedBits;
    Decoder decoder(FLAC__stream_decoder_new());
    require(decoder != nullptr, "Cannot allocate FLAC decoder");
    require(FLAC__stream_decoder_set_md5_checking(decoder.get(), false), "Cannot configure FLAC decoder");
    require(FLAC__stream_decoder_init_stream(decoder.get(), read_encoded, nullptr, nullptr, nullptr, nullptr,
            decoded, nullptr, decode_error, &state) == FLAC__STREAM_DECODER_INIT_STATUS_OK, "Cannot open FLAC decoder");
    const bool ok = FLAC__stream_decoder_process_until_end_of_stream(decoder.get());
    if (state.exception) {
        std::rethrow_exception(state.exception);
    }
    require(ok && !state.error && state.frames > 0, "FLAC packet decoding failed");
    require(FLAC__stream_decoder_finish(decoder.get()), "Cannot finish FLAC decoder");
    return std::move(state.output);
}

std::vector<uint8_t> FlacDecoder::flush()
{
    return {};
}
}
