#include "alac_audio.hpp"
#include <ALACEncoder.h>
#include <ALACDecoder.h>
#include <ALACBitUtilities.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace mjxl {
namespace {
void require(bool ok, const char* message)
{
    if (!ok) {
        throw std::runtime_error(message);
    }
}
uint32_t be32(const uint8_t* data)
{
    return uint32_t(data[0]) << 24 | uint32_t(data[1]) << 16 | uint32_t(data[2]) << 8 | data[3];
}
// ALAC 표준의 요소 순서를 공통 PCM의 스피커 순서에 대응시킨다.
constexpr unsigned channelOrder[8][8] = {
    {0}, {0, 1}, {2, 0, 1}, {2, 0, 1, 3}, {2, 0, 1, 3, 4},
    {2, 0, 1, 4, 5, 3}, {2, 0, 1, 4, 5, 6, 3}, {2, 6, 7, 0, 1, 4, 5, 3}
};
constexpr uint64_t channelMasks[8] = {4, 3, 7, 0x107, 0x37, 0x3f, 0x13f, 0xff};

}

bool alac_supported(const PcmFormat& format)
{
    return (format.bits == 8 || format.bits == 16 || format.bits == 24 || format.bits == 32) &&
           format.channels >= 1 && format.channels <= 8 && format.sampleRate > 0 && format.sampleRate <= 384000 &&
           (format.channelMask == 0 || format.channelMask == channelMasks[format.channels - 1]);
}

EncodedAudio alac_encode(std::span<const uint8_t> pcm, const PcmFormat& format)
{
    static_assert(std::endian::native == std::endian::little, "ALAC PCM adapter currently requires a little-endian host");
    require(alac_supported(format), "PCM layout cannot be stored as ALAC");
    const size_t sourceAlign = size_t(format.bits / 8) * format.channels;
    require(!pcm.empty() && pcm.size() % sourceAlign == 0, "PCM data is not sample aligned");
    EncodedAudio result;
    result.encoding = AudioEncoding::Alac;
    result.storedBits = format.bits == 8 ? 16 : format.bits;
    result.frameSamples = 4096;
    AudioFormatDescription input{};
    input.mSampleRate = format.sampleRate;
    input.mFormatID = kALACFormatLinearPCM;
    input.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    input.mChannelsPerFrame = format.channels;
    input.mBitsPerChannel = result.storedBits;
    input.mFramesPerPacket = 1;
    input.mBytesPerFrame = input.mBytesPerPacket = format.channels * result.storedBits / 8;
    AudioFormatDescription output{};
    output.mSampleRate = format.sampleRate;
    output.mFormatID = kALACFormatAppleLossless;
    output.mFormatFlags = result.storedBits == 16 ? 1 : result.storedBits == 24 ? 3 : 4;
    output.mChannelsPerFrame = format.channels;
    output.mFramesPerPacket = result.frameSamples;
    ALACEncoder encoder;
    require(encoder.InitializeEncoder(output) == 0, "Cannot open ALAC encoder");
    const size_t total = pcm.size() / sourceAlign;
    // 참조 인코더의 최대 출력 버퍼 공식과 정렬을 만족하는 독립 작업 버퍼를 사용한다.
    std::vector<uint32_t> inputWords((size_t(result.frameSamples) * input.mBytesPerFrame + 3) / 4);
    std::vector<uint8_t> encoded(size_t(result.frameSamples) * format.channels * 5 + 1);
    for (size_t position = 0; position < total;) {
        const auto count = static_cast<unsigned>(std::min<size_t>(result.frameSamples, total - position));
        auto bytes = reinterpret_cast<uint8_t*>(inputWords.data());
        for (unsigned frame = 0; frame < count; ++frame) {
            for (unsigned channel = 0; channel < format.channels; ++channel) {
                const size_t sourceSample = (position + frame) * format.channels + channelOrder[format.channels - 1][channel];
                const size_t targetSample = size_t(frame) * format.channels + channel;
                if (format.bits == 8) {
                    bytes[targetSample * 2] = 0;
                    bytes[targetSample * 2 + 1] = static_cast<uint8_t>(int(pcm[sourceSample]) - 128);
                } else {
                    std::memcpy(bytes + targetSample * (format.bits / 8), pcm.data() + sourceSample * (format.bits / 8), format.bits / 8);
                }
            }
        }
        int32_t size = static_cast<int32_t>(count * input.mBytesPerFrame);
        require(encoder.Encode(input, output, bytes, encoded.data(), &size) == 0 && size > 0 && size_t(size) <= encoded.size(),
                "ALAC encoding failed");
        result.packets.push_back({static_cast<int64_t>(position), {encoded.begin(), encoded.begin() + size}});
        position += count;
    }
    require(encoder.Finish() == 0, "Cannot finish ALAC encoder");
    uint32_t cookieSize = encoder.GetMagicCookieSize(format.channels);
    std::vector<uint8_t> cookie(cookieSize);
    encoder.GetMagicCookie(cookie.data(), &cookieSize);
    require(cookieSize >= 24, "ALAC encoder produced no configuration");
    // 트랙 채널 순서는 입력 순서를 보존한다. 컨테이너별 채널 배치는 별도 호환성 검증 대상이다.
    result.streamInfo.assign(cookie.begin(), cookie.begin() + 24);
    return result;
}

struct AlacDecoder::State {
    ALACDecoder decoder;
    PcmFormat format;
    unsigned storedBits = 0;
    unsigned frameSamples = 0;
};

AlacDecoder::AlacDecoder(std::span<const uint8_t> configuration, const PcmFormat& format) : state_(std::make_unique<State>())
{
    require(alac_supported(format) && configuration.size() == 24, "Invalid ALAC configuration");
    const auto frameSamples = be32(configuration.data());
    require(frameSamples >= 32 && frameSamples <= 16384 && configuration[4] == 0 &&
            configuration[5] == (format.bits == 8 ? 16 : format.bits) && configuration[6] == 40 &&
            configuration[7] == 10 && configuration[8] == 14 && configuration[9] == format.channels &&
            be32(configuration.data() + 20) == format.sampleRate, "ALAC configuration differs from original PCM");
    auto cookie = std::vector<uint8_t>(configuration.begin(), configuration.end());
    require(state_->decoder.Init(cookie.data(), static_cast<uint32_t>(cookie.size())) == 0, "Cannot open ALAC decoder");
    state_->format = format;
    state_->storedBits = configuration[5];
    state_->frameSamples = frameSamples;
}

AlacDecoder::~AlacDecoder() = default;

std::vector<uint8_t> AlacDecoder::decode(std::span<const uint8_t> packet)
{
    const auto& format = state_->format;
    const size_t maximum = size_t(state_->frameSamples) * format.channels * 5 + 1;
    require(packet.size() >= 3 && packet.size() <= maximum, "Invalid ALAC packet length");
    // 참조 디코더의 비트 선행 읽기에 필요한 패딩은 제공하되 논리적 끝은 실제 패킷에 둔다.
    std::vector<uint8_t> padded(packet.begin(), packet.end());
    padded.resize(packet.size() + 64, 0);
    std::vector<uint32_t> words((size_t(state_->frameSamples) * format.channels * (state_->storedBits / 8) + 3) / 4);
    BitBuffer bits;
    BitBufferInit(&bits, padded.data(), static_cast<uint32_t>(packet.size()));
    uint32_t count = 0;
    require(state_->decoder.Decode(&bits, reinterpret_cast<uint8_t*>(words.data()), state_->frameSamples, format.channels, &count) == 0 &&
            count > 0 && count <= state_->frameSamples && bits.cur <= bits.end, "ALAC decoding failed");
    auto bytes = reinterpret_cast<const uint8_t*>(words.data());
    const size_t samples = size_t(count) * format.channels;
    std::vector<uint8_t> output(samples * (format.bits / 8));
    for (unsigned frame = 0; frame < count; ++frame) {
        for (unsigned channel = 0; channel < format.channels; ++channel) {
            const size_t sourceSample = size_t(frame) * format.channels + channel;
            const size_t targetSample = size_t(frame) * format.channels + channelOrder[format.channels - 1][channel];
            if (format.bits == 8) {
                require(bytes[sourceSample * 2] == 0, "ALAC sample does not fit original PCM8");
                output[targetSample] = static_cast<uint8_t>(unsigned(bytes[sourceSample * 2 + 1]) + 128);
            } else {
                std::memcpy(output.data() + targetSample * (format.bits / 8), bytes + sourceSample * (format.bits / 8), format.bits / 8);
            }
        }
    }
    return output;
}
}
