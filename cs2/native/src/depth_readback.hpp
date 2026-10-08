#pragma once
#include "depth_sampler.hpp"
#include <json.hpp>
#include <array>
#include <optional>
#include <vector>

namespace cc {
struct CaptureBytes { std::string name; std::vector<uint8_t> bytes; };
struct DepthCaptureFrame { nlohmann::json metadata; std::vector<CaptureBytes> files; };
// Render-thread-only GPU ring. No host references survive enqueue(). Only owned
// staging resources reach later frames; the completed result has CPU bytes only.
class DepthReadback {
public:
    static constexpr size_t capacity=3;
    explicit DepthReadback(ID3D11Device*);
    bool enqueue(ID3D11DeviceContext*,ID3D11Texture2D*,nlohmann::json);
    std::optional<DepthCaptureFrame> poll(ID3D11DeviceContext*);
    size_t pending() const;
private:
    struct Buffer { Microsoft::WRL::ComPtr<ID3D11Buffer> staging; std::string name; UINT bytes{}; };
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
        Microsoft::WRL::ComPtr<ID3D11Query> ready;
        std::vector<Buffer> buffers;
        nlohmann::json metadata;
        UINT width{},height{};
        bool busy{};
    };
    ID3D11Device* device_;
    DepthSampler sampler_;
    std::array<Slot,capacity> slots_;
};
}
