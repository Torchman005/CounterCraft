#include "frame.hpp"
#include <json.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace cc {
namespace {
uint64_t little(std::span<const uint8_t> b, size_t offset, size_t count) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint64_t(b[offset + i]) << (i * 8);
    return value;
}
using json = nlohmann::json;
int64_t integer(const json& j, const char* key) {
    const auto& value = j.at(key);
    if (!value.is_number_integer() || (value.is_number_unsigned() && value.get<uint64_t>() > INT64_MAX))
        throw std::runtime_error(std::string("Invalid integer: ") + key);
    return value.get<int64_t>();
}
double number(const json& value) {
    if (!value.is_number()) throw std::runtime_error("Expected finite number");
    double result = value.get<double>();
    if (!std::isfinite(result)) throw std::runtime_error("Expected finite number");
    return result;
}
template<size_t N> std::array<double, N> vector(const json& value) {
    if (!value.is_array() || value.size() != N) throw std::runtime_error("Invalid camera/matrix vector");
    std::array<double, N> result{};
    for (size_t i = 0; i < N; ++i) result[i] = number(value[i]);
    return result;
}
void flag(const json& j, const char* name, bool expected) {
    if (!j.at(name).is_boolean() || j.at(name).get<bool>() != expected) throw std::runtime_error("Invalid frame flag");
}
}

Session Session::parse(const std::string& uuid) {
    if (uuid.size() != 36 || uuid[8] != '-' || uuid[13] != '-' || uuid[18] != '-' || uuid[23] != '-')
        throw std::runtime_error("Invalid session UUID");
    std::string hex;
    for (char c : uuid) {
        if (c == '-') continue;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            throw std::runtime_error("Invalid UUID digit");
        hex += c;
    }
    return {std::stoull(hex.substr(0, 16), nullptr, 16), std::stoull(hex.substr(16), nullptr, 16)};
}
std::string Session::str() const {
    char hex[33]{};
    std::snprintf(hex, sizeof(hex), "%016llx%016llx", static_cast<unsigned long long>(high), static_cast<unsigned long long>(low));
    std::string s(hex);
    return s.substr(0,8) + "-" + s.substr(8,4) + "-" + s.substr(12,4) + "-" + s.substr(16,4) + "-" + s.substr(20);
}
Header decode_header(std::span<const uint8_t> bytes, Session expected, uint64_t previous) {
    if (bytes.size() != 64 || std::string(reinterpret_cast<const char*>(bytes.data()), 8) != "CCFRM001"
        || little(bytes,8,4) != 1 || little(bytes,12,4) != 64 || little(bytes,28,4) || little(bytes,60,4))
        throw std::runtime_error("Invalid binary frame header");
    Header h{{little(bytes,32,8),little(bytes,40,8)},little(bytes,48,8),
        uint32_t(little(bytes,16,4)),uint32_t(little(bytes,20,4)),uint32_t(little(bytes,24,4)),uint32_t(little(bytes,56,4))};
    if (!(h.session == expected) || h.sequence <= previous || h.sequence > INT64_MAX
        || !h.metadata_bytes || h.metadata_bytes > max_metadata || !h.color_bytes || h.color_bytes > max_pixels * 4
        || h.color_bytes != h.depth_bytes || h.color_bytes % 4)
        throw std::runtime_error("Wrong session, sequence or oversized packet");
    return h;
}
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t previous) {
    static const std::array<uint32_t,256> table = [] {
        std::array<uint32_t,256> values{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t n = i;
            for (int bit = 0; bit < 8; ++bit) n = (n >> 1) ^ (n & 1 ? 0xedb88320u : 0);
            values[i] = n;
        }
        return values;
    }();
    uint32_t crc = ~previous;
    for (uint8_t byte : bytes) crc = table[(crc ^ byte) & 255] ^ (crc >> 8);
    return ~crc;
}
Metadata decode_metadata(const std::string& text, const Header& h, int64_t epoch) {
    if (text.empty() || text.size() > max_metadata) throw std::runtime_error("Metadata too large");
    const json j = json::parse(text, [](int depth, json::parse_event_t, json&) {
        if (depth > 8) throw std::runtime_error("JSON nesting limit");
        return true;
    });
    if (!j.is_object() || integer(j,"v") != 1 || j.at("type") != "world-stream-frame"
        || j.at("rowOrder") != "bottom-to-top" || j.at("colorEncoding") != "rgba8"
        || j.at("depthEncoding") != "float32-le" || j.at("depthSpace") != "opengl-window-z")
        throw std::runtime_error("Unexpected frame metadata");
    flag(j,"reversedZ",false); flag(j,"includesHandHud",false); flag(j,"includesSkyFog",true);
    Metadata m;
    int64_t width = integer(j,"width"), height = integer(j,"height");
    if (width < 1 || height < 1 || width > max_pixels || height > max_pixels
        || uint64_t(width) * uint64_t(height) * 4 != h.color_bytes) throw std::runtime_error("Invalid frame dimensions");
    m.width = int(width); m.height = int(height); m.epoch = integer(j,"epoch");
    m.requested_frame = integer(j,"requestedFrame"); m.captured_ns = integer(j,"monotonicNanos");
    m.readback_ns = integer(j,"readbackNanos");
    m.near_plane = number(j.at("near")); m.far_plane = number(j.at("far"));
    m.position = vector<3>(j.at("camera").at("position")); m.rotation = vector<3>(j.at("camera").at("rotation"));
    m.fov = number(j.at("camera").at("fov"));
    m.projection = vector<16>(j.at("projectionColumnMajor")); m.view_rotation = vector<16>(j.at("viewRotationColumnMajor"));
    if (m.epoch != epoch || m.requested_frame < -1 || m.readback_ns < 0 || m.near_plane <= 0
        || m.far_plane <= m.near_plane || m.fov <= 0 || m.fov >= 180) throw std::runtime_error("Invalid frame camera/epoch/planes");
    return m;
}
void validate_depth(std::span<const uint8_t> bytes) {
    if (bytes.size() % 4) throw std::runtime_error("Incomplete depth samples");
    for (size_t i = 0; i < bytes.size(); i += 4) {
        const float depth = std::bit_cast<float>(uint32_t(little(bytes,i,4)));
        if (!std::isfinite(depth) || depth < 0 || depth > 1) throw std::runtime_error("Invalid depth sample");
    }
}
double linear_depth(double depth, double n, double f) {
    if (!std::isfinite(depth) || !std::isfinite(n) || !std::isfinite(f) || depth < 0 || depth > 1 || n <= 0 || f <= n)
        throw std::runtime_error("Invalid perspective depth");
    return n * f / (f - depth * (f - n));
}
int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
