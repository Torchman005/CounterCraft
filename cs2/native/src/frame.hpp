#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <optional>
#include "host_camera.hpp"

namespace cc {
constexpr uint32_t max_pixels = 4'194'304, max_metadata = 4096;
struct Session {
    uint64_t high{}, low{};
    bool operator==(const Session&) const = default;
    static Session parse(const std::string& uuid);
    std::string str() const;
};
struct Header {
    Session session;
    uint64_t sequence{};
    uint32_t metadata_bytes{}, color_bytes{}, depth_bytes{}, crc{};
};
struct Metadata {
    int width{}, height{};
    bool full_client{}, gui_open{};
    int64_t epoch{}, requested_frame{}, captured_ns{}, readback_ns{};
    double near_plane{}, far_plane{}, fov{};
    std::array<double, 3> position{}, rotation{};
    std::array<double, 16> projection{}, view_rotation{};
};
struct Frame {
    Header header;
    Metadata metadata;
    std::vector<uint8_t> pixels; // GL rows, RGBA then little-endian float32 depth.
    int64_t received_ns{};
    std::optional<HostCamera> relayed_camera; // Set only after rendered-pose validation by Receiver.
    std::span<const uint8_t> rgba() const { return {pixels.data(), header.color_bytes}; }
    std::span<const uint8_t> depth() const { return {pixels.data() + header.color_bytes, header.depth_bytes}; }
};
Header decode_header(std::span<const uint8_t> bytes, Session expected, uint64_t previous_sequence);
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t previous = 0);
Metadata decode_metadata(const std::string& json, const Header&, int64_t expected_epoch);
void validate_depth(std::span<const uint8_t> bytes);
double linear_depth(double depth, double near_plane, double far_plane);
int64_t monotonic_ns();
}
