#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace cc {
// Copies a depth texture into owned storage, then extracts raw sample min/max.
// No ResolveSubresource on depth, no guessed Z convention, no host-view ownership.
// Only CS shader/SRV0/UAV0 state is changed and restored by sample().
class DepthSampler {
public:
    explicit DepthSampler(ID3D11Device*);
    ~DepthSampler();
    DepthSampler(const DepthSampler&)=delete;
    DepthSampler& operator=(const DepthSampler&)=delete;
    bool same_device(ID3D11Device*) const;
    void prepare(const D3D11_TEXTURE2D_DESC&);
    void sample(ID3D11DeviceContext*,ID3D11Texture2D*);
    ID3D11Texture2D* output() const { return output_.Get(); }
    const D3D11_TEXTURE2D_DESC& description() const { return source_desc_; }
private:
    ID3D11Device* device_; // caller guarantees device outlives owned resources
    GUID identity_{};
    bool marked_{};
    D3D11_TEXTURE2D_DESC source_desc_{};
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> single_,multi_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_,output_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> target_;
};
}
