#pragma once
#include "flac_audio.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <span>
#include <vector>

namespace mjxl {
/** @brief Creates a little-endian FOURCC value. */
constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint8_t(a) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
/** @brief Experimental Motion JPEG XL identifier. */
constexpr auto MJXL = fourcc('M', 'J', 'X', 'L');
/** @brief Exact rational frame rate, independent of a container backend. */
struct Rational {
    int num = 0; /**< Numerator. */
    int den = 1; /**< Positive denominator. */
};
/** @brief Buffered media packet with its physical source-file location. */
struct Packet {
    bool video;                 /**< True for video, false for audio. */
    std::vector<uint8_t> bytes;  /**< Complete packet bytes. */
    int64_t position;           /**< Source byte offset, or negative if unavailable. */
};
/** @brief Current single-video, optional-audio buffered reader contract. */
struct Movie {
    unsigned width = 0;      /**< Video width. */
    unsigned height = 0;     /**< Video height. */
    unsigned sampleRate = 0; /**< Audio sample rate. */
    unsigned channels = 0;   /**< Interleaved audio channel count. */
    unsigned bits = 0;       /**< Original PCM depth, including compressed audio. */
    uint64_t channelMask = 0; /**< Native audio speaker mask when known. */
    Rational fps;            /**< Constant video frame rate. */
    bool jxl = false;        /**< True for independent JPEG XL video packets. */
    bool alac = false;       /**< True for ALAC audio packets. */
    bool flac = false;       /**< True for FLAC audio packets. */
    std::vector<uint8_t> streamInfo; /**< FLAC STREAMINFO or ALAC configuration, if present. */
    std::vector<Packet> packets;    /**< Media packets in demux order. */
};
/** @brief Packet writer abstraction; does not expose platform or FFmpeg types. */
class ContainerWriter {
public:
    /** @brief Releases writer resources. */
    virtual ~ContainerWriter() = default;
    /** @brief Writes one complete video or audio packet. */
    virtual void packet(bool video, std::span<const uint8_t> data) = 0;
    /** @brief Finalizes the file, optionally storing AVI recovery data. */
    virtual void finish(std::span<const uint8_t> original = {}) = 0;
};
/** @brief Reads the currently supported media profile and physical packet locations. */
Movie read_movie(const std::filesystem::path& path);
/** @brief Reads recovery payload from AVI, MKV or MOV; returns empty when absent. */
std::vector<uint8_t> read_original_chunk(const std::filesystem::path& path);
/** @brief Opens the native classic-AVI writer without overwriting an existing file. */
std::unique_ptr<ContainerWriter> make_avi_writer(const std::filesystem::path& path, const Movie& movie,
                                               uint32_t videoCodec = MJXL, const FlacStream* flac = nullptr);
/** @brief Opens the transitional FFmpeg Matroska writer with optional recovery payload. */
std::unique_ptr<ContainerWriter> make_matroska_writer(const std::filesystem::path& path, const Movie& movie,
                                                    const FlacStream* flac, std::span<const uint8_t> original);
/** @brief Writes native MOV with ALAC or PCM and private recovery atoms. */
std::unique_ptr<ContainerWriter> make_mov_writer(const std::filesystem::path& path, const Movie& movie,
                                               const EncodedAudio* audio, std::span<const uint8_t> original);
/** @brief Reads a bounded private MOV atom, returning no value when absent. */
std::optional<std::vector<uint8_t>> read_mov_metadata(const std::filesystem::path& path, std::string_view requested);
/** @brief Configures diagnostic logging for the transitional container backend. */
void configure_container_logging();
}
