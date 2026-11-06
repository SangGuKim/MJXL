#include "audio_policy.hpp"
#include "alac_audio.hpp"
#include <stdexcept>

namespace mjxl {
AudioCodec parse_audio_codec(std::string_view name)
{
    if (name == "auto") {
        return AudioCodec::Auto;
    }
    if (name == "flac") {
        return AudioCodec::Flac;
    }
    if (name == "alac") {
        return AudioCodec::Alac;
    }
    if (name == "copy") {
        return AudioCodec::Copy;
    }
    throw std::invalid_argument("--audio-codec requires auto, flac, alac or copy");
}

std::string_view audio_codec_name(AudioCodec codec)
{
    switch (codec) {
    case AudioCodec::Auto: return "auto";
    case AudioCodec::Flac: return "flac";
    case AudioCodec::Alac: return "alac";
    case AudioCodec::Copy: return "copy";
    }
    throw std::invalid_argument("Invalid audio codec");
}

AudioSelection select_audio(Container container, AudioCodec requested, const PcmFormat& format)
{
    if (requested == AudioCodec::Auto) {
        if (container == Container::Mov) {
            if (alac_supported(format)) {
                return {AudioCodec::Alac, "MOV defaults to Apple Lossless"};
            }
            return {AudioCodec::Copy, "PCM format or speaker layout is outside the ALAC profile"};
        }
        if (flac_supported(format)) {
            return {AudioCodec::Flac, "AVI/MKV default to FLAC for this PCM format"};
        }
        return {AudioCodec::Copy, "PCM format is outside the current FLAC profile"};
    }
    if (requested == AudioCodec::Flac && !flac_supported(format)) {
        throw std::invalid_argument("The requested FLAC codec cannot preserve this PCM format; use auto or copy");
    }
    if (requested == AudioCodec::Alac && !alac_supported(format)) {
        throw std::invalid_argument("The requested ALAC codec cannot preserve this PCM format or speaker layout; use auto or copy");
    }
    return {requested, "Explicit audio codec selection"};
}
}
