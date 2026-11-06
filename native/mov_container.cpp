#include "container.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace mjxl {
namespace {
void require(bool ok, const char* message)
{
    if (!ok) {
        throw std::runtime_error(message);
    }
}
struct Atoms {
    std::vector<uint8_t> bytes;
    void integer(uint64_t value, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            bytes.push_back(static_cast<uint8_t>(value >> ((count - i - 1) * 8)));
        }
    }
    void zeros(size_t count)
    {
        bytes.insert(bytes.end(), count, 0);
    }
    void tag(std::string_view value)
    {
        require(value.size() == 4, "Invalid MOV atom type");
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    size_t begin(std::string_view type, int version = -1, uint32_t flags = 0)
    {
        const auto at = bytes.size();
        integer(0, 4); tag(type);
        if (version >= 0) {
            integer(version, 1); integer(flags, 3);
        }
        return at;
    }
    void end(size_t at)
    {
        const auto size = bytes.size() - at;
        require(size <= UINT32_MAX, "MOV metadata exceeds the atom size limit");
        for (size_t i = 0; i < 4; ++i) {
            bytes[at + i] = static_cast<uint8_t>(size >> ((3 - i) * 8));
        }
    }
    void append(std::span<const uint8_t> data)
    {
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void matrix()
    {
        for (const uint32_t v : {0x10000u, 0u, 0u, 0u, 0x10000u, 0u, 0u, 0u, 0x40000000u}) {
            integer(v, 4);
        }
    }
};
struct Sample {
    uint64_t offset;
    uint32_t bytes, count, duration;
};

class MovWriter final : public ContainerWriter {
    std::fstream file_;
    Movie movie_;
    EncodedAudio audio_;
    bool compressed_ = false;
    std::vector<Sample> video_, sound_;
    uint64_t mdat_ = 0, totalAudio_ = 0, writtenAudio_ = 0;
    std::vector<uint8_t> original_;

    uint64_t position()
    {
        const auto value = file_.tellp();
        require(value >= 0, "Cannot query MOV output position");
        return static_cast<uint64_t>(value);
    }
    void write(std::span<const uint8_t> data)
    {
        file_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        require(bool(file_), "Cannot write MOV output");
    }
    uint64_t media_duration(bool video) const
    {
        return video ? uint64_t(video_.size()) * movie_.fps.den : writtenAudio_;
    }
    uint64_t movie_duration(bool video) const
    {
        const uint64_t scale = video ? movie_.fps.num : movie_.sampleRate;
        const uint64_t duration = media_duration(video);
        require(duration <= (UINT64_MAX - scale) / 1000, "MOV duration overflow");
        return (duration * 1000 + scale - 1) / scale;
    }
    void track(Atoms& a, bool video)
    {
        const auto& samples = video ? video_ : sound_;
        const auto trak = a.begin("trak");
        const auto tkhd = a.begin("tkhd", 1, 7);
        a.zeros(16); a.integer(video ? 1 : 2, 4); a.zeros(4); a.integer(movie_duration(video), 8);
        a.zeros(8); a.zeros(4); a.integer(video ? 0 : 0x100, 2); a.zeros(2); a.matrix();
        a.integer(video ? movie_.width << 16 : 0, 4); a.integer(video ? movie_.height << 16 : 0, 4); a.end(tkhd);
        const auto mdia = a.begin("mdia");
        const auto mdhd = a.begin("mdhd", 1);
        a.zeros(16); a.integer(video ? movie_.fps.num : movie_.sampleRate, 4);
        a.integer(media_duration(video), 8); a.integer(0x55c4, 2); a.zeros(2); a.end(mdhd);
        const auto hdlr = a.begin("hdlr", 0);
        a.zeros(4); a.tag(video ? "vide" : "soun"); a.zeros(12);
        const std::string_view name = video ? "MJXL Video" : "MJXL Audio";
        a.bytes.insert(a.bytes.end(), name.begin(), name.end()); a.zeros(1); a.end(hdlr);
        const auto minf = a.begin("minf");
        const auto mediaHeader = a.begin(video ? "vmhd" : "smhd", 0, video ? 1 : 0);
        a.zeros(video ? 8 : 4); a.end(mediaHeader);
        const auto dinf = a.begin("dinf"); const auto dref = a.begin("dref", 0); a.integer(1, 4);
        const auto url = a.begin("url ", 0, 1); a.end(url); a.end(dref); a.end(dinf);
        const auto stbl = a.begin("stbl");
        const auto stsd = a.begin("stsd", 0); a.integer(1, 4);
        const auto entry = a.begin(video ? "MJXL" : compressed_ ? "alac" : "lpcm");
        a.zeros(6); a.integer(1, 2);
        if (video) {
            a.zeros(16); a.integer(movie_.width, 2); a.integer(movie_.height, 2);
            a.integer(0x480000, 4); a.integer(0x480000, 4); a.zeros(4); a.integer(1, 2);
            a.zeros(32); a.integer(24, 2); a.integer(0xffff, 2);
        } else if (compressed_ && movie_.sampleRate <= 65535) {
            a.zeros(8); a.integer(movie_.channels, 2); a.integer(audio_.storedBits, 2); a.zeros(4);
            a.integer(movie_.sampleRate <= 65535 ? movie_.sampleRate << 16 : 0, 4);
            const auto config = a.begin("alac", 0); a.append(audio_.streamInfo); a.end(config);
        } else {
            // QuickTime sound description v2로 little-endian 정수 PCM의 깊이와 샘플률을 명시한다.
            a.integer(2, 2); a.zeros(6); a.integer(3, 2); a.integer(16, 2); a.integer(0xfffe, 2); a.zeros(2);
            a.integer(0x10000, 4); a.integer(72, 4);
            a.integer(std::bit_cast<uint64_t>(double(movie_.sampleRate)), 8);
            a.integer(movie_.channels, 4); a.integer(0x7f000000, 4); a.integer(compressed_ ? audio_.storedBits : movie_.bits, 4);
            a.integer(compressed_ ? 0 : movie_.bits == 8 ? 8 : 12, 4);
            a.integer(compressed_ ? 0 : movie_.channels * movie_.bits / 8, 4);
            a.integer(compressed_ ? audio_.frameSamples : 1, 4);
            if (compressed_) {
                const auto config = a.begin("alac", 0); a.append(audio_.streamInfo); a.end(config);
            }
        }
        if (!video && !compressed_ && movie_.channels > 2 && movie_.channelMask) {
            require(movie_.channelMask <= UINT32_MAX, "Unsupported MOV PCM speaker bitmap");
            const auto chan = a.begin("chan", 0);
            a.integer(0x10000, 4); a.integer(movie_.channelMask, 4); a.zeros(4); a.end(chan);
        }
        a.end(entry); a.end(stsd);
        const auto stts = a.begin("stts", 0);
        std::vector<std::pair<uint32_t, uint32_t>> runs;
        for (const auto& sample : samples) {
            const uint32_t count = video || compressed_ ? 1 : sample.count;
            const uint32_t duration = video ? sample.duration : compressed_ ? sample.duration : 1;
            if (!runs.empty() && runs.back().second == duration && runs.back().first <= UINT32_MAX - count) {
                runs.back().first += count;
            } else {
                runs.emplace_back(count, duration);
            }
        }
        a.integer(runs.size(), 4);
        for (const auto& [count, duration] : runs) {
            a.integer(count, 4); a.integer(duration, 4);
        }
        a.end(stts);
        const auto stsc = a.begin("stsc", 0);
        std::vector<std::pair<uint32_t, uint32_t>> chunks;
        for (size_t i = 0; i < samples.size(); ++i) {
            const uint32_t count = video || compressed_ ? 1 : samples[i].count;
            if (chunks.empty() || chunks.back().second != count) {
                chunks.emplace_back(static_cast<uint32_t>(i + 1), count);
            }
        }
        a.integer(chunks.size(), 4);
        for (const auto& [first, count] : chunks) {
            a.integer(first, 4); a.integer(count, 4); a.integer(1, 4);
        }
        a.end(stsc);
        const auto stsz = a.begin("stsz", 0);
        const bool pcm = !video && !compressed_;
        a.integer(pcm ? movie_.channels * movie_.bits / 8 : 0, 4);
        require(!pcm || writtenAudio_ <= UINT32_MAX, "PCM MOV exceeds the current sample-table limit");
        a.integer(pcm ? writtenAudio_ : samples.size(), 4);
        if (!pcm) {
            for (const auto& sample : samples) {
                a.integer(sample.bytes, 4);
            }
        }
        a.end(stsz);
        const auto co64 = a.begin("co64", 0); a.integer(samples.size(), 4);
        for (const auto& sample : samples) {
            a.integer(sample.offset, 8);
        }
        a.end(co64); a.end(stbl); a.end(minf); a.end(mdia); a.end(trak);
    }
public:
    MovWriter(const std::filesystem::path& path, const Movie& movie, const EncodedAudio* audio, std::span<const uint8_t> original)
        : compressed_(audio != nullptr), original_(original.begin(), original.end())
    {
        movie_.width = movie.width; movie_.height = movie.height; movie_.fps = movie.fps;
        movie_.channels = movie.channels; movie_.sampleRate = movie.sampleRate; movie_.bits = movie.bits;
        movie_.channelMask = movie.channelMask;
        require(!std::filesystem::exists(path), "Output or partial output already exists");
        require(movie.width <= 65535 && movie.height <= 65535 && movie.fps.num > 0 && movie.fps.den > 0, "Unsupported MOV video format");
        if (audio) {
            require(audio->encoding == AudioEncoding::Alac && audio->streamInfo.size() == 24, "MOV supports ALAC or PCM copy");
            audio_.encoding = audio->encoding; audio_.storedBits = audio->storedBits;
            audio_.frameSamples = audio->frameSamples; audio_.streamInfo = audio->streamInfo;
        }
        for (const auto& packet : movie.packets) {
            if (!packet.video) {
                totalAudio_ += packet.bytes.size();
            }
        }
        if (movie.channels) {
            const auto align = movie.channels * movie.bits / 8;
            require(align > 0 && totalAudio_ % align == 0, "PCM input is not sample aligned");
            totalAudio_ /= align;
        }
        file_.open(path, std::ios::binary | std::ios::out | std::ios::in | std::ios::trunc);
        require(bool(file_), "Cannot create MOV output");
        Atoms header; const auto ftyp = header.begin("ftyp"); header.tag("qt  "); header.zeros(4); header.tag("qt  "); header.end(ftyp); write(header.bytes);
        mdat_ = position(); Atoms data; data.integer(1, 4); data.tag("mdat"); data.zeros(8); write(data.bytes);
    }
    void packet(bool video, std::span<const uint8_t> data) override
    {
        require(!data.empty() && data.size() <= UINT32_MAX, "Invalid MOV packet size");
        uint32_t count = 1, duration = movie_.fps.den;
        if (!video) {
            require(movie_.channels > 0 && writtenAudio_ < totalAudio_, "Unexpected MOV audio packet");
            if (compressed_) {
                count = duration = static_cast<uint32_t>(std::min<uint64_t>(audio_.frameSamples, totalAudio_ - writtenAudio_));
            } else {
                const auto align = movie_.channels * movie_.bits / 8;
                require(data.size() % align == 0 && data.size() / align <= totalAudio_ - writtenAudio_, "Invalid PCM MOV packet");
                count = duration = static_cast<uint32_t>(data.size() / align);
            }
            writtenAudio_ += duration;
        }
        auto& samples = video ? video_ : sound_;
        require(samples.size() < UINT32_MAX, "Too many MOV packets");
        samples.push_back({position(), static_cast<uint32_t>(data.size()), count, duration});
        write(data);
    }
    void finish(std::span<const uint8_t> = {}) override
    {
        require(!video_.empty() && writtenAudio_ == totalAudio_, "Incomplete MOV media");
        const auto end = position();
        file_.seekp(static_cast<std::streamoff>(mdat_ + 8)); Atoms size; size.integer(end - mdat_, 8); write(size.bytes);
        file_.seekp(static_cast<std::streamoff>(end));
        Atoms a; const auto moov = a.begin("moov"); const auto mvhd = a.begin("mvhd", 1);
        a.zeros(16); a.integer(1000, 4); a.integer(std::max(movie_duration(true), movie_.channels ? movie_duration(false) : 0), 8);
        a.integer(0x10000, 4); a.integer(0x100, 2); a.zeros(10); a.matrix(); a.zeros(24); a.integer(3, 4); a.end(mvhd);
        track(a, true);
        if (movie_.channels) {
            track(a, false);
        }
        const auto udta = a.begin("udta");
        if (compressed_) {
            const auto bits = a.begin("MJXA"); a.integer(1, 4); a.integer(movie_.bits, 4); a.end(bits);
        }
        if (!original_.empty()) {
            const auto recovery = a.begin("MJXO"); a.append(original_); a.end(recovery);
        }
        a.end(udta); a.end(moov); write(a.bytes); file_.close();
        require(!file_.fail(), "Cannot close MOV output");
    }
};
uint64_t read_integer(std::ifstream& file, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto byte = file.get();
        require(byte != EOF, "Truncated MOV atom");
        value = value << 8 | static_cast<uint8_t>(byte);
    }
    return value;
}
void scan(std::ifstream& file, uint64_t begin, uint64_t end, std::string_view requested, std::optional<std::vector<uint8_t>>& result, unsigned depth = 0)
{
    require(depth <= 8, "MOV metadata nesting exceeds the supported depth");
    for (uint64_t at = begin; at < end;) {
        require(end - at >= 8, "Truncated MOV atom header");
        file.seekg(static_cast<std::streamoff>(at));
        uint64_t size = read_integer(file, 4); char type[4]; file.read(type, 4); uint64_t header = 8;
        if (size == 1) {
            require(end - at >= 16, "Truncated extended MOV atom");
            size = read_integer(file, 8); header = 16;
        } else if (size == 0) {
            size = end - at;
        }
        require(size >= header && size <= end - at, "Invalid MOV atom bounds");
        const std::string_view tag(type, 4);
        if (tag == requested) {
            require(!result.has_value() && size - header <= (uint64_t(1) << 32), "Duplicate or oversized MJXL MOV metadata");
            result.emplace(static_cast<size_t>(size - header));
            file.read(reinterpret_cast<char*>(result->data()), static_cast<std::streamsize>(result->size()));
            require(bool(file), "Truncated MJXL MOV metadata");
        } else if (tag == "moov" || tag == "udta") {
            scan(file, at + header, at + size, requested, result, depth + 1);
        }
        at += size;
    }
}
}

std::unique_ptr<ContainerWriter> make_mov_writer(const std::filesystem::path& path, const Movie& movie,
                                               const EncodedAudio* audio, std::span<const uint8_t> original)
{
    return std::make_unique<MovWriter>(path, movie, audio, original);
}

std::optional<std::vector<uint8_t>> read_mov_metadata(const std::filesystem::path& path, std::string_view requested)
{
    std::ifstream file(path, std::ios::binary);
    char header[8]{}; file.read(header, 8);
    if (!file || (std::memcmp(header + 4, "ftyp", 4) != 0 && std::memcmp(header + 4, "wide", 4) != 0 &&
                  std::memcmp(header + 4, "mdat", 4) != 0 && std::memcmp(header + 4, "moov", 4) != 0)) {
        return std::nullopt;
    }
    std::optional<std::vector<uint8_t>> result;
    scan(file, 0, std::filesystem::file_size(path), requested, result);
    return result;
}
}
