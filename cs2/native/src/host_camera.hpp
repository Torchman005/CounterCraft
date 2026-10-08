#pragma once
#include <array>
#include <cstdint>
#include <json.hpp>
#include <span>
#include <vector>

namespace cc {
using CameraMatrix=std::array<double,16>; // row-major mathematical matrix
struct CameraLocation { uint32_t slot{},offset{}; bool column_major{}; };
struct CameraLayout {
    CameraLocation view,projection,world_vp,relative_vp;
    static CameraLayout parse(const nlohmann::json&);
};
struct CameraBuffer {
    uint32_t slot{},first_constant{},num_constants{};
    std::span<const uint8_t> bytes;
};
struct HostCamera {
    CameraMatrix view{},projection{};
    std::array<double,6> viewport{};
    std::array<double,3> position{},forward{},up{};
    double yaw{},pitch{},roll{},fov{},aspect{},near_plane{},far_plane{};
    bool right_handed{},reversed{};
    nlohmann::json report() const;
};
// Reads only owned CPU copies of public VS constant-buffer bindings. Every set
// must revalidate view, projection and both independent matrix products. Layout
// is calibrated from local evidence, never a compiled retail ABI or memory scan.
HostCamera decode_host_camera(const CameraLayout&,std::span<const CameraBuffer>,const std::array<double,6>& viewport);
}
