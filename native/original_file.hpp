#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace mjxl {
/** @brief Location of one media packet in the original file. */
struct OriginalPacket {
    bool video = false;    /**< True for a video packet and false for an audio packet. */
    uint64_t position = 0; /**< Byte offset in the original file. */
    uint32_t size = 0;     /**< Packet size in bytes. */
};

/**
 * @brief Packs everything needed to rebuild the original file from its media packets.
 * @param file Complete original file.
 * @param packets Media packets in reading order. They must not overlap.
 * @return Payload holding the packet layout, the remaining bytes and a SHA-256 hash.
 *
 * Packet bytes are not stored because the encoded container already holds them.
 */
std::vector<uint8_t> pack_original(std::span<const uint8_t> file, const std::vector<OriginalPacket>& packets);

/**
 * @brief Rebuilds the exact original file and checks its SHA-256 hash.
 * @param payload Result of pack_original().
 * @param video Original video packets in reading order.
 * @param audio Original audio packet bytes joined in reading order.
 * @return Complete original file.
 */
std::vector<uint8_t> rebuild_original(std::span<const uint8_t> payload, const std::vector<std::vector<uint8_t>>& video, std::span<const uint8_t> audio);
}
