#include "core.hpp"
#include <jxl/decode.h>
#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <memory>
#include <stdexcept>

namespace mjxl {
static void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

Codec::Codec(unsigned workers)
{
    runner_ = JxlThreadParallelRunnerCreate(nullptr, workers);
    require(runner_ != nullptr, "Cannot create JPEG XL worker pool");
}

Codec::~Codec()
{
    JxlThreadParallelRunnerDestroy(runner_);
}

std::vector<uint8_t> Codec::encode(std::span<const uint8_t> input, int effort) const
{
    std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> enc(JxlEncoderCreate(nullptr), JxlEncoderDestroy);
    require(enc != nullptr, "Cannot create JPEG XL encoder");
    require(JxlEncoderSetParallelRunner(enc.get(), JxlThreadParallelRunner, runner_) == JXL_ENC_SUCCESS, "Cannot set encoder workers");
    require(JxlEncoderUseContainer(enc.get(), JXL_TRUE) == JXL_ENC_SUCCESS, "Cannot enable JXL container");
    require(JxlEncoderStoreJPEGMetadata(enc.get(), JXL_TRUE) == JXL_ENC_SUCCESS, "Cannot enable JPEG reconstruction");
    auto settings = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
    require(settings != nullptr, "Cannot create JXL settings");
    require(JxlEncoderFrameSettingsSetOption(settings, JXL_ENC_FRAME_SETTING_EFFORT, effort) == JXL_ENC_SUCCESS, "Cannot set JXL effort");
    require(JxlEncoderAddJPEGFrame(settings, input.data(), input.size()) == JXL_ENC_SUCCESS, "JPEG is unsupported by libjxl");
    JxlEncoderCloseInput(enc.get());
    std::vector<uint8_t> result;
    for (;;) {
        const auto offset = result.size();
        require(offset < 256ULL * 1024 * 1024, "Encoded frame exceeds limit");
        result.resize(offset + 65536);
        auto next = result.data() + offset;
        size_t available = 65536;
        const auto status = JxlEncoderProcessOutput(enc.get(), &next, &available);
        result.resize(offset + 65536 - available);
        if (status == JXL_ENC_SUCCESS) {
            return result;
        }
        require(status == JXL_ENC_NEED_MORE_OUTPUT, "JPEG XL encoding failed");
    }
}

std::vector<uint8_t> Codec::jpeg(std::span<const uint8_t> input) const
{
    std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> dec(JxlDecoderCreate(nullptr), JxlDecoderDestroy);
    require(dec != nullptr, "Cannot create JPEG XL decoder");
    require(JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE) == JXL_DEC_SUCCESS, "Cannot subscribe to reconstruction");
    require(JxlDecoderSetInput(dec.get(), input.data(), input.size()) == JXL_DEC_SUCCESS, "Invalid JXL input");
    JxlDecoderCloseInput(dec.get());
    std::vector<uint8_t> result, buffer(65536);
    bool started = false;
    for (;;) {
        const auto status = JxlDecoderProcessInput(dec.get());
        if (status == JXL_DEC_JPEG_RECONSTRUCTION || status == JXL_DEC_JPEG_NEED_MORE_OUTPUT) {
            if (started) {
                const auto unused = JxlDecoderReleaseJPEGBuffer(dec.get());
                result.insert(result.end(), buffer.begin(), buffer.end() - unused);
            }
            require(result.size() < 256ULL * 1024 * 1024, "Restored JPEG exceeds limit");
            require(JxlDecoderSetJPEGBuffer(dec.get(), buffer.data(), buffer.size()) == JXL_DEC_SUCCESS, "Cannot set JPEG buffer");
            started = true;
        } else if (status == JXL_DEC_FULL_IMAGE || status == JXL_DEC_SUCCESS) {
            require(started, "JXL frame has no JPEG reconstruction data");
            const auto unused = JxlDecoderReleaseJPEGBuffer(dec.get());
            result.insert(result.end(), buffer.begin(), buffer.end() - unused);
            return result;
        } else {
            throw std::runtime_error("JPEG reconstruction failed");
        }
    }
}

std::vector<uint8_t> Codec::rgb(std::span<const uint8_t> input, unsigned width, unsigned height) const
{
    require(width > 0 && height > 0 && width <= 16384 && height <= 16384 && uint64_t(width) * height <= 64ULL * 1024 * 1024, "Invalid frame dimensions");
    std::unique_ptr<JxlDecoder, decltype(&JxlDecoderDestroy)> dec(JxlDecoderCreate(nullptr), JxlDecoderDestroy);
    require(dec != nullptr, "Cannot create JPEG XL decoder");
    require(JxlDecoderSetParallelRunner(dec.get(), JxlThreadParallelRunner, runner_) == JXL_DEC_SUCCESS, "Cannot set decoder workers");
    require(JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) == JXL_DEC_SUCCESS, "Cannot subscribe to pixel decoding");
    require(JxlDecoderSetInput(dec.get(), input.data(), input.size()) == JXL_DEC_SUCCESS, "Invalid JXL input");
    JxlDecoderCloseInput(dec.get());
    JxlPixelFormat format{3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    std::vector<uint8_t> output;
    for (;;) {
        const auto status = JxlDecoderProcessInput(dec.get());
        if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info{};
            require(JxlDecoderGetBasicInfo(dec.get(), &info) == JXL_DEC_SUCCESS, "Cannot read JXL dimensions");
            require(info.xsize == width && info.ysize == height && !info.have_animation && info.orientation == JXL_ORIENT_IDENTITY, "Unexpected JXL dimensions or orientation");
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            size_t size = 0;
            require(JxlDecoderImageOutBufferSize(dec.get(), &format, &size) == JXL_DEC_SUCCESS && size == size_t(width) * height * 3, "Invalid RGB buffer size");
            output.resize(size);
            require(JxlDecoderSetImageOutBuffer(dec.get(), &format, output.data(), output.size()) == JXL_DEC_SUCCESS, "Cannot set RGB buffer");
        } else if (status == JXL_DEC_FULL_IMAGE) {
            require(!output.empty(), "Missing RGB output");
            return output;
        } else {
            throw std::runtime_error("JPEG XL pixel decoding failed");
        }
    }
}
}
