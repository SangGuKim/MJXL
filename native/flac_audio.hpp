#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mjxl {
/** @brief Layout of the original PCM samples. */
struct PcmFormat {
    unsigned bits = 0;       /**< Bits per sample of the original PCM. */
    unsigned channels = 0;   /**< Number of interleaved channels. */
    unsigned sampleRate = 0; /**< Samples per second for each channel. */
    uint64_t channelMask = 0; /**< Native speaker mask, or zero for the codec default. */
};

/** @brief One encoded lossless audio frame and the position of its first sample. */
struct FlacPacket {
    int64_t position = 0;       /**< Index of the first sample per channel. */
    std::vector<uint8_t> bytes; /**< Encoded FLAC frame. */
};

/** @brief Lossless audio bitstream type. */
enum class AudioEncoding { Flac, Alac };

/** @brief Complete lossless audio stream produced from one PCM buffer. */
struct EncodedAudio {
    AudioEncoding encoding = AudioEncoding::Flac; /**< Codec identifying the configuration and packets. */
    std::vector<uint8_t> streamInfo; /**< FLAC STREAMINFO or 24-byte ALAC configuration. */
    std::vector<FlacPacket> packets; /**< Encoded frames in stream order. */
    unsigned frameSamples = 0;       /**< Samples per channel in a full frame. */
    unsigned storedBits = 0;         /**< Bit depth recorded in the compressed stream. */
};

/** @brief Backward-compatible name for the FLAC encoder result. */
using FlacStream = EncodedAudio;

/**
 * @brief Tells whether the PCM layout can be stored as FLAC and restored exactly.
 * @param format Original PCM layout.
 * @return True for 8, 16 and 24-bit PCM with up to eight channels.
 */
bool flac_supported(const PcmFormat& format);

/**
 * @brief Losslessly encodes interleaved PCM as FLAC.
 * @param pcm Unsigned 8-bit or signed little-endian 16/24-bit samples.
 * @param format Original PCM layout.
 * @return Encoded frames and the final STREAMINFO block.
 *
 * For compatibility, 8-bit PCM occupies the upper byte of a 16-bit FLAC sample.
 */
FlacStream flac_encode(std::span<const uint8_t> pcm, const PcmFormat& format);

/** @brief Decodes FLAC frames back to the original PCM byte layout. */
class FlacDecoder {
public:
    /**
     * @brief Opens a decoder.
     * @param streamInfo FLAC STREAMINFO block.
     * @param format Original PCM layout to restore.
     */
    FlacDecoder(std::span<const uint8_t> streamInfo, const PcmFormat& format);
    /** @brief Releases the decoder. */
    ~FlacDecoder();
    FlacDecoder(const FlacDecoder&) = delete;
    FlacDecoder& operator=(const FlacDecoder&) = delete;
    /** @brief Decodes one FLAC frame and returns the PCM bytes that became available. */
    std::vector<uint8_t> decode(std::span<const uint8_t> packet);
    /** @brief Returns PCM bytes still held by the decoder at the end of the stream. */
    std::vector<uint8_t> flush();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
