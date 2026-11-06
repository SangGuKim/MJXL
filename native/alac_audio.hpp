#pragma once
#include "flac_audio.hpp"

namespace mjxl {
/** @brief Tests the supported integer PCM profile for ALAC. */
bool alac_supported(const PcmFormat& format);
/** @brief Encodes PCM using Apple's portable ALAC implementation; PCM8 is promoted reversibly. */
EncodedAudio alac_encode(std::span<const uint8_t> pcm, const PcmFormat& format);
/** @brief Decodes an ALAC packet into original little-endian PCM, including unsigned PCM8. */
class AlacDecoder {
public:
    /** @brief Validates and opens a 24-byte ALAC configuration for the original PCM layout. */
    AlacDecoder(std::span<const uint8_t> configuration, const PcmFormat& format);
    /** @brief Releases decoder resources. */
    ~AlacDecoder();
    AlacDecoder(const AlacDecoder&) = delete;
    AlacDecoder& operator=(const AlacDecoder&) = delete;
    /** @brief Decodes one bounded ALAC packet. */
    std::vector<uint8_t> decode(std::span<const uint8_t> packet);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
