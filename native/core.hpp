#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace mjxl {
/** @brief Encodes independent JPEG XL frames and restores original JPEG bytes. */
class Codec {
public:
    /** @brief Creates a reusable worker pool. */
    explicit Codec(unsigned workers = 4);
    /** @brief Releases the worker pool. */
    ~Codec();
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    /** @brief Recompresses a JPEG while retaining exact reconstruction data. */
    std::vector<uint8_t> encode(std::span<const uint8_t> jpeg) const;
    /** @brief Reconstructs the exact original JPEG bytes. */
    std::vector<uint8_t> jpeg(std::span<const uint8_t> input) const;
    /** @brief Decodes an independent JXL frame to top-down packed RGB8. */
    std::vector<uint8_t> rgb(std::span<const uint8_t> input, unsigned width, unsigned height) const;
private:
    void* runner_ = nullptr;
};
}
