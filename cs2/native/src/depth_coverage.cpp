#include "depth_coverage.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <objbase.h>

namespace cc {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT h,const char* why){if(FAILED(h))throw std::runtime_error(why);}
const char* source=R"HLSL(
Texture2D<float2> world:register(t0);Texture2D<float2> current:register(t1);
RWTexture2D<float> coverage:register(u0);
cbuffer Baseline:register(b0){uint cleared;float clear;uint2 reserved;}
[numthreads(8,8,1)]void cs(uint3 p:SV_DispatchThreadID){
    uint w,h;coverage.GetDimensions(w,h);if(p.x>=w || p.y>=h)return;
    float2 a=cleared?float2(clear,clear):world.Load(int3(p.xy,0));
    float2 b=current.Load(int3(p.xy,0));
    if(any(a!=b) || any(isnan(a)) || any(isinf(a)))coverage[p.xy]=1;
})HLSL";
struct State {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11Buffer> cb;
    std::array<ID3D11ShaderResourceView*,2> srvs{};
    std::array<ID3D11ClassInstance*,256> classes{};UINT count=256;
    ComPtr<ID3D11DeviceContext1> c1;UINT first{},constants{};
    State(ID3D11DeviceContext* context):c(context){
        c->CSGetShader(&shader,classes.data(),&count);c->CSGetShaderResources(0,2,srvs.data());c->CSGetUnorderedAccessViews(0,1,&uav);
        c->QueryInterface(IID_PPV_ARGS(&c1));if(c1)c1->CSGetConstantBuffers1(0,1,&cb,&first,&constants);else c->CSGetConstantBuffers(0,1,&cb);
    }
    ~State(){UINT keep=UINT(-1);auto* u=uav.Get();auto* b=cb.Get();
        c->CSSetShader(shader.Get(),classes.data(),count);c->CSSetShaderResources(0,2,srvs.data());c->CSSetUnorderedAccessViews(0,1,&u,&keep);
        if(c1)c1->CSSetConstantBuffers1(0,1,&b,&first,&constants);else c->CSSetConstantBuffers(0,1,&b);
        for(auto* s:srvs)if(s)s->Release();for(UINT i=0;i<count;++i)if(classes[i])classes[i]->Release();
    }
};
}
void DepthCoverage::shape(ID3D11Texture2D* t,D3D11_TEXTURE2D_DESC& desc)const{
    if(!t)throw std::runtime_error("Missing coverage depth");t->GetDesc(&desc);
    ComPtr<ID3D11Device> actual;t->GetDevice(&actual);
    if(!same_device(actual.Get()) || desc.Format!=DXGI_FORMAT_R32G32_FLOAT || desc.MipLevels!=1 || desc.ArraySize!=1
        || desc.SampleDesc.Count!=1 || !desc.Width || !desc.Height || uint64_t(desc.Width)*desc.Height>4096ULL*4096)
        throw std::runtime_error("Unsupported coverage depth/device");
}
DepthCoverage::DepthCoverage(ID3D11Device* device):device_(device){
    if(!device)throw std::runtime_error("Missing coverage device");
    ComPtr<ID3DBlob> code,errors;auto h=D3DCompile(source,std::strlen(source),"DepthCoverage",nullptr,nullptr,"cs","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(h) && errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    ok(h,"Compile coverage");ok(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader_),"Create coverage shader");
    D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ok(device->CreateBuffer(&b,nullptr,&constants_),"Create coverage constants");
    ok(CoCreateGuid(&identity_),"Create coverage device identity");
    constexpr UINT marker=0x43434356;
    ok(device->SetPrivateData(identity_,sizeof(marker),&marker),"Mark coverage device");marked_=true;
}
DepthCoverage::~DepthCoverage(){if(marked_)device_->SetPrivateData(identity_,0,nullptr);}
bool DepthCoverage::same_device(ID3D11Device* actual)const{
    UINT marker{},bytes=sizeof(marker);
    return actual && SUCCEEDED(actual->GetPrivateData(identity_,&bytes,&marker)) && bytes==sizeof(marker) && marker==0x43434356;
}
void DepthCoverage::reset(ID3D11DeviceContext* context,ID3D11Texture2D* world){
    D3D11_TEXTURE2D_DESC desc{};shape(world,desc);
    ComPtr<ID3D11Device> actual;if(context)context->GetDevice(&actual);
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || !same_device(actual.Get()))throw std::runtime_error("Coverage context changed");
    D3D11_TEXTURE2D_DESC old{};if(output_)output_->GetDesc(&old);
    if(!output_ || old.Width!=desc.Width || old.Height!=desc.Height){
        desc.Format=DXGI_FORMAT_R32_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;
        desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> output;ComPtr<ID3D11UnorderedAccessView> target;ComPtr<ID3D11ShaderResourceView> view;
        ok(device_->CreateTexture2D(&desc,nullptr,&output),"Create coverage texture");
        ok(device_->CreateUnorderedAccessView(output.Get(),nullptr,&target),"Create coverage UAV");
        ok(device_->CreateShaderResourceView(output.Get(),nullptr,&view),"Create coverage SRV");
        output_=std::move(output);target_=std::move(target);view_=std::move(view);
    }
    const float zeros[4]{};context->ClearUnorderedAccessViewFloat(target_.Get(),zeros);
}
void DepthCoverage::accumulate(ID3D11DeviceContext* context,ID3D11Texture2D* world,ID3D11Texture2D* current,bool cleared,float clear){
    D3D11_TEXTURE2D_DESC a{},b{},out{};shape(world,a);shape(current,b);
    if(!output_)throw std::runtime_error("Uninitialized coverage");output_->GetDesc(&out);
    ComPtr<ID3D11Device> actual;if(context)context->GetDevice(&actual);
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || !same_device(actual.Get())
        || a.Width!=b.Width || a.Height!=b.Height || a.Width!=out.Width || a.Height!=out.Height)
        throw std::runtime_error("Coverage shape/context changed");
    auto bind=[&](ID3D11Texture2D* texture,ComPtr<ID3D11ShaderResourceView>& view){
        ComPtr<ID3D11Resource> prior;if(view)view->GetResource(&prior);
        if(prior.Get()!=texture){view.Reset();ok(device_->CreateShaderResourceView(texture,nullptr,&view),"Coverage input SRV");}
    };
    bind(world,world_view_);bind(current,current_view_);
    struct Parameters{UINT cleared;float clear;UINT reserved[2];} parameters{cleared?1u:0u,clear,{}};
    State state(context);context->UpdateSubresource(constants_.Get(),0,nullptr,&parameters,0,0);
    auto* cb=constants_.Get();auto* u=target_.Get();UINT keep=UINT(-1);ID3D11ShaderResourceView* srvs[]{world_view_.Get(),current_view_.Get()};
    context->CSSetShader(shader_.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,&cb);
    context->CSSetShaderResources(0,2,srvs);context->CSSetUnorderedAccessViews(0,1,&u,&keep);
    context->Dispatch((a.Width+7)/8,(a.Height+7)/8,1);
    ID3D11ShaderResourceView* none[2]{};context->CSSetShaderResources(0,2,none);ID3D11UnorderedAccessView* empty=nullptr;context->CSSetUnorderedAccessViews(0,1,&empty,&keep);
}
}
