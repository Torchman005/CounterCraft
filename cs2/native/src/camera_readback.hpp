#pragma once
#include "host_camera.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <optional>

namespace cc {
struct CameraCopy {
    uint32_t slot{},first{},count{};
    std::vector<uint8_t> bytes;
};
struct CameraSample {
    uint64_t sequence{};
    int64_t captured_ns{};
    std::array<double,6> viewport{};
    std::vector<CameraCopy> copies;
    HostCamera decode(const CameraLayout&) const;
};
// Render-thread-owned ring: read only owned staging with DONOTFLUSH/DO_NOT_WAIT.
// Source buffer references last only for enqueue(). No depth, shaders or graphics
// state changes are needed to deliver these small camera packets to the worker.
class CameraReadback {
public:
    explicit CameraReadback(ID3D11Device*,CameraLayout);
    bool enqueue(ID3D11DeviceContext*,uint64_t sequence,int64_t captured_ns,const std::array<double,6>&);
    std::optional<CameraSample> poll(ID3D11DeviceContext*);
    size_t pending() const;
private:
    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        uint32_t slot{},bytes{},first{},count{};
    };
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> ready;
        std::vector<Buffer> buffers;
        uint64_t sequence{};
        int64_t captured_ns{};
        std::array<double,6> viewport{};
        bool busy{};
    };
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    CameraLayout layout_;
    std::array<Slot,3> slots_;
    void validate(ID3D11DeviceContext*) const;
};
}
