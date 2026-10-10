#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace cc {
// Retains every changed pixel between the immutable world snapshot and clears.
class DepthCoverage {
public:
    explicit DepthCoverage(ID3D11Device*);
    ~DepthCoverage();
    DepthCoverage(const DepthCoverage&)=delete;
    DepthCoverage& operator=(const DepthCoverage&)=delete;
    void reset(ID3D11DeviceContext*,ID3D11Texture2D* world);
    void accumulate(ID3D11DeviceContext*,ID3D11Texture2D* world,ID3D11Texture2D* current,
                    bool baseline_cleared,float clear,bool cross_resource=false);
    ID3D11ShaderResourceView* view()const{return view_.Get();}
    ID3D11Texture2D* output()const{return output_.Get();}
private:
    void shape(ID3D11Texture2D*,D3D11_TEXTURE2D_DESC&)const;
    bool same_device(ID3D11Device*)const;
    GUID identity_{};
    bool marked_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> target_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> world_view_,current_view_;
};
}
