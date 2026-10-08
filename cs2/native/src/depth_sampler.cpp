#include "depth_sampler.hpp"
#include <d3dcompiler.h>
#include <objbase.h>
#include <array>
#include <cstring>
#include <stdexcept>

namespace cc {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(what); }
ComPtr<ID3D11ComputeShader> shader(ID3D11Device* device,bool ms) {
    const char* source=R"HLSL(
#ifdef MULTISAMPLE
Texture2DMS<float> sourceDepth : register(t0);
#else
Texture2D<float> sourceDepth : register(t0);
#endif
RWTexture2D<float2> result : register(u0);
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    uint w,h; result.GetDimensions(w,h); if(p.x>=w || p.y>=h) return;
#ifdef MULTISAMPLE
    uint sw,sh,n; sourceDepth.GetDimensions(sw,sh,n);
    float lo=1,hi=0;
    for(uint s=0;s<n;++s) { float d=sourceDepth.Load(p.xy,s); lo=min(lo,d); hi=max(hi,d); }
    result[p.xy]=float2(lo,hi);
#else
    float d=sourceDepth.Load(int3(p.xy,0)); result[p.xy]=float2(d,d);
#endif
})HLSL";
    D3D_SHADER_MACRO macros[]={{"MULTISAMPLE","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,errors;
    const auto hr=D3DCompile(source,std::strlen(source),"CounterCraftDepthSampler",ms?macros:nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    ok(hr,"Compile depth sampler"); ComPtr<ID3D11ComputeShader> result;
    ok(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&result),"Create depth sampler");
    return result;
}
std::pair<DXGI_FORMAT,DXGI_FORMAT> formats(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: return {DXGI_FORMAT_R16_TYPELESS,DXGI_FORMAT_R16_UNORM};
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: return {DXGI_FORMAT_R24G8_TYPELESS,DXGI_FORMAT_R24_UNORM_X8_TYPELESS};
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: return {DXGI_FORMAT_R32_TYPELESS,DXGI_FORMAT_R32_FLOAT};
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: return {DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS};
    default: throw std::runtime_error("Unsupported depth format");
    }
}
struct ComputeState {
    ID3D11DeviceContext* context;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    std::array<ID3D11ClassInstance*,256> classes{};
    UINT class_count=UINT(classes.size());
    explicit ComputeState(ID3D11DeviceContext* c):context(c) {
        c->CSGetShader(&shader,classes.data(),&class_count);
        c->CSGetShaderResources(0,1,&srv); c->CSGetUnorderedAccessViews(0,1,&uav);
    }
    ~ComputeState() {
        ID3D11UnorderedAccessView* none=nullptr; UINT keep=UINT(-1);
        context->CSSetUnorderedAccessViews(0,1,&none,&keep);
        auto* r=srv.Get(); auto* u=uav.Get();
        context->CSSetShaderResources(0,1,&r); context->CSSetUnorderedAccessViews(0,1,&u,&keep);
        context->CSSetShader(shader.Get(),classes.data(),class_count);
        for(UINT i=0;i<class_count;++i) if(classes[i]) classes[i]->Release();
    }
};
}
DepthSampler::DepthSampler(ID3D11Device* device):device_(device) {
    if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0) throw std::runtime_error("D3D11 sampler requires FL11");
    single_=shader(device,false); multi_=shader(device,true);
    // ReShade's native context returns the original device, but hooked resource
    // GetDevice returns its proxy. Both forward public device private-data calls
    // to the SAME underlying object. A unique, non-COM marker verifies this
    // without accepting arbitrary adapters/devices or retaining a proxy alias.
    ok(CoCreateGuid(&identity_),"Create device identity key");
    constexpr uint32_t marker=0x43434450;
    ok(device_->SetPrivateData(identity_,sizeof(marker),&marker),"Mark owned sampler device identity"); marked_=true;
}
DepthSampler::~DepthSampler() { if(marked_) device_->SetPrivateData(identity_,0,nullptr); }
bool DepthSampler::same_device(ID3D11Device* actual) const {
    uint32_t marker=0; UINT bytes=sizeof(marker);
    return actual && SUCCEEDED(actual->GetPrivateData(identity_,&bytes,&marker)) && bytes==sizeof(marker) && marker==0x43434450;
}
void DepthSampler::prepare(const D3D11_TEXTURE2D_DESC& desc) {
    if(!desc.Width || !desc.Height || uint64_t(desc.Width)*desc.Height>4096ULL*4096 || desc.ArraySize!=1 || desc.MipLevels!=1
        || !(desc.BindFlags&D3D11_BIND_DEPTH_STENCIL) || desc.SampleDesc.Count<1 || desc.SampleDesc.Count>8)
        throw std::runtime_error("Unsupported depth shape");
    const auto [resource_format,srv_format]=formats(desc.Format);
    if(copy_ && desc.Width==source_desc_.Width && desc.Height==source_desc_.Height && desc.Format==source_desc_.Format
        && desc.SampleDesc.Count==source_desc_.SampleDesc.Count && desc.SampleDesc.Quality==source_desc_.SampleDesc.Quality) return;
    auto input_desc=desc; input_desc.Format=resource_format; input_desc.Usage=D3D11_USAGE_DEFAULT;
    input_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE; input_desc.CPUAccessFlags=0; input_desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> copy,output; ComPtr<ID3D11ShaderResourceView> input;
    ComPtr<ID3D11UnorderedAccessView> target;
    ok(device_->CreateTexture2D(&input_desc,nullptr,&copy),"Create owned depth copy");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=srv_format;
    view.ViewDimension=desc.SampleDesc.Count>1?D3D11_SRV_DIMENSION_TEXTURE2DMS:D3D11_SRV_DIMENSION_TEXTURE2D;
    if(desc.SampleDesc.Count==1) view.Texture2D.MipLevels=1;
    ok(device_->CreateShaderResourceView(copy.Get(),&view,&input),"Create owned depth SRV");
    auto output_desc=input_desc; output_desc.Format=DXGI_FORMAT_R32G32_FLOAT;
    output_desc.SampleDesc={1,0}; output_desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
    ok(device_->CreateTexture2D(&output_desc,nullptr,&output),"Create depth min/max output");
    ok(device_->CreateUnorderedAccessView(output.Get(),nullptr,&target),"Create depth output UAV");
    copy_=std::move(copy); input_=std::move(input); output_=std::move(output); target_=std::move(target); source_desc_=desc;
}
void DepthSampler::sample(ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || !source || !copy_)
        throw std::runtime_error("Unprepared/immediate depth sampler");
    D3D11_TEXTURE2D_DESC desc; source->GetDesc(&desc);
    if(desc.Width!=source_desc_.Width || desc.Height!=source_desc_.Height || desc.Format!=source_desc_.Format
        || desc.SampleDesc.Count!=source_desc_.SampleDesc.Count || desc.SampleDesc.Quality!=source_desc_.SampleDesc.Quality
        || desc.MipLevels!=1 || desc.ArraySize!=1) throw std::runtime_error("Depth lifetime/shape changed");
    ComPtr<ID3D11Device> actual; source->GetDevice(&actual);
    if(!same_device(actual.Get())) throw std::runtime_error("Depth device mismatch");
    context->GetDevice(&actual);
    if(!same_device(actual.Get())) throw std::runtime_error("Depth context device mismatch");
    ComputeState state(context);
    context->CopyResource(copy_.Get(),source);
    auto* srv=input_.Get(); auto* uav=target_.Get(); UINT keep=UINT(-1);
    context->CSSetShader(source_desc_.SampleDesc.Count>1?multi_.Get():single_.Get(),nullptr,0);
    context->CSSetShaderResources(0,1,&srv); context->CSSetUnorderedAccessViews(0,1,&uav,&keep);
    context->Dispatch((desc.Width+7)/8,(desc.Height+7)/8,1);
    ID3D11ShaderResourceView* none=nullptr; context->CSSetShaderResources(0,1,&none);
}
}
