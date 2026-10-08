#pragma once
#include "host_camera.hpp"
#include <d3d11.h>
#include <wrl/client.h>

namespace cc {
// Current-frame owned matrix texture. It allows an effect to compare today's
// rays with a previously validated guest camera without waiting for CPU readback.
class CameraGpu {
public:
    CameraGpu(ID3D11Device*,CameraLayout);
    void capture(ID3D11DeviceContext*);
    ID3D11ShaderResourceView* view()const{return view_.Get();}
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    CameraLayout layout_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,4> copies_;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,4> inputs_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> target_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
};
}
