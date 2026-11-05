#include "original_file.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <brotli/decode.h>
#include <brotli/encode.h>
extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace mjxl {
namespace {
constexpr uint32_t kVersion = 1;
constexpr size_t kHashSize = 32;
// 비디오와 음성을 뺀 나머지 바이트는 컨테이너 정보뿐이므로 이 크기를 넘으면 손상으로 본다.
constexpr uint64_t kResidueLimit = 1ULL << 32;
constexpr uint64_t kTableLimit = 256ULL * 1024 * 1024;

void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::array<uint8_t, kHashSize> sha256(std::span<const uint8_t> data)
{
    AVSHA* context = av_sha_alloc();
    require(context != nullptr, "Cannot allocate SHA-256 context");
    std::array<uint8_t, kHashSize> result{};
    av_sha_init(context, 256);
    av_sha_update(context, data.data(), data.size());
    av_sha_final(context, result.data());
    av_free(context);
    return result;
}

std::vector<uint8_t> compress(std::span<const uint8_t> data)
{
    if (data.empty()) {
        return {};
    }
    size_t size = BrotliEncoderMaxCompressedSize(data.size());
    require(size != 0, "Original container data is too large");
    std::vector<uint8_t> output(size);
    require(BrotliEncoderCompress(BROTLI_MAX_QUALITY, BROTLI_MAX_WINDOW_BITS, BROTLI_MODE_GENERIC, data.size(), data.data(), &size, output.data()) == BROTLI_TRUE,
            "Cannot compress original container data");
    output.resize(size);
    return output;
}

// 선언된 크기를 믿고 미리 할당하지 않고 실제로 풀린 만큼만 메모리를 늘린다.
std::vector<uint8_t> decompress(std::span<const uint8_t> data, size_t expected)
{
    std::vector<uint8_t> output;
    if (expected == 0) {
        require(data.empty(), "Invalid original container data");
        return output;
    }
    std::unique_ptr<BrotliDecoderState, decltype(&BrotliDecoderDestroyInstance)> state(BrotliDecoderCreateInstance(nullptr, nullptr, nullptr), BrotliDecoderDestroyInstance);
    require(state != nullptr, "Cannot create Brotli decoder");
    std::vector<uint8_t> buffer(1 << 16);
    size_t availableIn = data.size();
    const uint8_t* nextIn = data.data();
    for (;;) {
        size_t availableOut = buffer.size();
        uint8_t* nextOut = buffer.data();
        const auto result = BrotliDecoderDecompressStream(state.get(), &availableIn, &nextIn, &availableOut, &nextOut, nullptr);
        require(result != BROTLI_DECODER_RESULT_ERROR, "Cannot decompress original container data");
        const size_t produced = buffer.size() - availableOut;
        require(produced <= expected - output.size(), "Original container data is larger than declared");
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(produced));
        if (result == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        }
        require(result != BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT, "Truncated original container data");
    }
    require(output.size() == expected && availableIn == 0, "Original container data has an unexpected size");
    return output;
}

void put_varint(std::vector<uint8_t>& output, uint64_t value)
{
    while (value >= 0x80) {
        output.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    output.push_back(static_cast<uint8_t>(value));
}

template <typename T>
void put_value(std::vector<uint8_t>& output, T value)
{
    const auto bytes = reinterpret_cast<const uint8_t*>(&value);
    output.insert(output.end(), bytes, bytes + sizeof(T));
}

// 길이를 넘어 읽지 않도록 모든 필드를 이 읽기 도구로만 해석한다.
class Reader {
    std::span<const uint8_t> data_;
    size_t position_ = 0;
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data)
    {
    }
    template <typename T>
    T value()
    {
        require(data_.size() - position_ >= sizeof(T), "Truncated original container data");
        T result{};
        std::memcpy(&result, data_.data() + position_, sizeof(T));
        position_ += sizeof(T);
        return result;
    }
    std::span<const uint8_t> bytes(size_t count)
    {
        require(data_.size() - position_ >= count, "Truncated original container data");
        const auto result = data_.subspan(position_, count);
        position_ += count;
        return result;
    }
    uint64_t varint()
    {
        uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            const auto byte = value<uint8_t>();
            result |= uint64_t(byte & 0x7f) << shift;
            if (!(byte & 0x80)) {
                return result;
            }
        }
        throw std::runtime_error("Invalid original container layout");
    }
    bool empty() const
    {
        return position_ == data_.size();
    }
};

struct Span {
    uint64_t position;
    uint64_t size;
    const uint8_t* source;
};

// 패킷들이 파일 안에 겹치지 않고 들어가는지 확인한 뒤 위치 순서로 정렬한다.
void sort_spans(std::vector<Span>& spans, uint64_t fileSize)
{
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.position < b.position; });
    uint64_t end = 0;
    for (const auto& span : spans) {
        require(span.position >= end && span.size <= fileSize && span.position <= fileSize - span.size, "Original packets overlap or exceed the file");
        end = span.position + span.size;
    }
}
}

std::vector<uint8_t> pack_original(std::span<const uint8_t> file, const std::vector<OriginalPacket>& packets)
{
    std::vector<Span> spans;
    spans.reserve(packets.size());
    std::vector<uint8_t> table;
    int64_t previousEnd = 0;
    for (const auto& packet : packets) {
        require(packet.position <= uint64_t((std::numeric_limits<int64_t>::max)()), "Original packet position is too large");
        spans.push_back({packet.position, packet.size, nullptr});
        // 대부분의 패킷은 앞 패킷 끝에 이어지므로 차이만 저장한다. 영상 크기는 복원한 JPEG에서 알 수 있다.
        const int64_t delta = static_cast<int64_t>(packet.position) - previousEnd;
        put_varint(table, (static_cast<uint64_t>(delta) << 1) ^ static_cast<uint64_t>(delta >> 63));
        put_varint(table, packet.video ? 1 : uint64_t(packet.size) << 1);
        previousEnd = static_cast<int64_t>(packet.position + packet.size);
    }
    sort_spans(spans, file.size());
    std::vector<uint8_t> residue;
    uint64_t cursor = 0;
    for (const auto& span : spans) {
        residue.insert(residue.end(), file.begin() + static_cast<ptrdiff_t>(cursor), file.begin() + static_cast<ptrdiff_t>(span.position));
        cursor = span.position + span.size;
    }
    residue.insert(residue.end(), file.begin() + static_cast<ptrdiff_t>(cursor), file.end());
    require(residue.size() <= kResidueLimit && table.size() <= kTableLimit, "Original container data is too large");

    const auto packedTable = compress(table);
    const auto packedResidue = compress(residue);
    std::vector<uint8_t> payload;
    put_value<uint32_t>(payload, kVersion);
    put_value<uint64_t>(payload, file.size());
    const auto hash = sha256(file);
    payload.insert(payload.end(), hash.begin(), hash.end());
    put_value<uint32_t>(payload, static_cast<uint32_t>(packets.size()));
    put_value<uint32_t>(payload, static_cast<uint32_t>(table.size()));
    put_value<uint32_t>(payload, static_cast<uint32_t>(packedTable.size()));
    put_value<uint32_t>(payload, static_cast<uint32_t>(packedResidue.size()));
    payload.insert(payload.end(), packedTable.begin(), packedTable.end());
    payload.insert(payload.end(), packedResidue.begin(), packedResidue.end());
    return payload;
}

std::vector<uint8_t> rebuild_original(std::span<const uint8_t> payload, const std::vector<std::vector<uint8_t>>& video, std::span<const uint8_t> audio)
{
    Reader reader(payload);
    require(reader.value<uint32_t>() == kVersion, "Unknown original container data version");
    const auto fileSize = reader.value<uint64_t>();
    require(fileSize <= uint64_t((std::numeric_limits<int64_t>::max)()), "Invalid original file size");
    std::array<uint8_t, kHashSize> expected{};
    const auto storedHash = reader.bytes(kHashSize);
    std::copy(storedHash.begin(), storedHash.end(), expected.begin());
    const auto packetCount = reader.value<uint32_t>();
    const auto tableSize = reader.value<uint32_t>();
    const auto packedTableSize = reader.value<uint32_t>();
    const auto packedResidueSize = reader.value<uint32_t>();
    require(tableSize <= kTableLimit, "Invalid original container layout");
    const auto packedTable = reader.bytes(packedTableSize);
    const auto packedResidue = reader.bytes(packedResidueSize);
    require(reader.empty(), "Invalid original container data length");

    const auto table = decompress(packedTable, tableSize);
    Reader layout(table);
    std::vector<Span> spans;
    spans.reserve((std::min<size_t>)(packetCount, table.size()));
    size_t videoIndex = 0;
    uint64_t audioOffset = 0;
    uint64_t previousEnd = 0;
    uint64_t mediaBytes = 0;
    for (uint32_t i = 0; i < packetCount; ++i) {
        const auto zigzag = layout.varint();
        const uint64_t delta = (zigzag >> 1) ^ (0 - (zigzag & 1));
        const auto entry = layout.varint();
        // 부호 없는 덧셈으로 음수 차이를 표현하므로 값이 잘못되어도 아래 파일 범위 검사에서 걸러진다.
        Span span{previousEnd + delta, 0, nullptr};
        if (entry == 1) {
            require(videoIndex < video.size(), "Original layout has too many video packets");
            span.size = video[videoIndex].size();
            span.source = video[videoIndex].data();
            ++videoIndex;
        } else {
            require((entry & 1) == 0, "Invalid original packet layout");
            span.size = entry >> 1;
            require(span.size <= audio.size() && audioOffset <= audio.size() - span.size, "Original layout has too many audio bytes");
            span.source = audio.data() + audioOffset;
            audioOffset += span.size;
        }
        require(span.size <= fileSize && span.position <= fileSize - span.size, "Original packets exceed the file");
        previousEnd = span.position + span.size;
        mediaBytes += span.size;
        spans.push_back(span);
    }
    require(layout.empty() && videoIndex == video.size() && audioOffset == audio.size(), "Original layout does not match the media packets");
    sort_spans(spans, fileSize);
    require(mediaBytes <= fileSize && fileSize - mediaBytes <= kResidueLimit, "Invalid original file size");

    const auto residue = decompress(packedResidue, static_cast<size_t>(fileSize - mediaBytes));
    std::vector<uint8_t> file(static_cast<size_t>(fileSize));
    uint64_t cursor = 0;
    size_t residueOffset = 0;
    const auto copy_residue = [&](uint64_t until) {
        const auto count = static_cast<size_t>(until - cursor);
        if (count) {
            std::memcpy(file.data() + cursor, residue.data() + residueOffset, count);
        }
        residueOffset += count;
    };
    for (const auto& span : spans) {
        copy_residue(span.position);
        if (span.size) {
            std::memcpy(file.data() + span.position, span.source, static_cast<size_t>(span.size));
        }
        cursor = span.position + span.size;
    }
    copy_residue(fileSize);
    require(sha256(file) == expected, "Rebuilt file does not match the original SHA-256 hash");
    return file;
}
}
