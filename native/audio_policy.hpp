#pragma once
#include "flac_audio.hpp"
#include <string_view>

namespace mjxl {
/** @brief Output container selected by the caller. */
enum class Container { Avi, Matroska, Mov };
/** @brief Requested audio representation; Copy preserves the original PCM layout. */
enum class AudioCodec { Auto, Flac, Alac, Copy };
/** @brief Resolved audio policy, independent of codec backend availability. */
struct AudioSelection {
    AudioCodec codec;         /**< Resolved representation. */
    std::string_view reason;  /**< Human-readable reason for this selection. */
};
/** @brief Parses auto, flac, alac or copy; throws for other names. */
AudioCodec parse_audio_codec(std::string_view name);
/** @brief Returns the stable CLI name of a codec. */
std::string_view audio_codec_name(AudioCodec codec);
/** @brief Resolves policy; explicit unsupported FLAC formats throw instead of falling back. */
AudioSelection select_audio(Container container, AudioCodec requested, const PcmFormat& format);
}
