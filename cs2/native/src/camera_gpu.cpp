#include "camera_gpu.hpp"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>

namespace cc {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr,const char* why){if(FAILED(hr))throw std::runtime_error(why);}
const char* camera_shader=R"HLSL(
ByteAddressBuffer a:register(t0);ByteAddressBuffer b:register(t1);
ByteAddressBuffer c:register(t2);ByteAddressBuffer d:register(t3);
RWTexture2D<float4> result:register(u0);
cbuffer Layout:register(b0){uint4 offsets;uint4 columns;}
uint load(uint m,uint offset){if(m==0)return a.Load(offset);if(m==1)return b.Load(offset);if(m==2)return c.Load(offset);return d.Load(offset);}
[numthreads(4,4,1)]void cs(uint3 id:SV_DispatchThreadID) {
    uint r=id.x,m=id.y;float4 row;
    for(uint k=0;k<4;k++)row[k]=asfloat(load(m,offsets[m]+4*(columns[m]?k*4+r:r*4+k)));
    result[uint2(r,m)]=row;
})HLSL";
struct State {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11ComputeShader> shader;
    std::array<ID3D11ClassInstance*,256> classes{};UINT count=256;
    std::array<ID3D11ShaderResourceView*,4> srvs{};
    ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11DeviceContext1> c1;UINT first{},constants{};
    State(ID3D11DeviceContext* context):c(context){
        c->CSGetShader(&shader,classes.data(),&count);c->CSGetShaderResources(0,4,srvs.data());c->CSGetUnorderedAccessViews(0,1,&uav);
        c->QueryInterface(IID_PPV_ARGS(&c1));if(c1)c1->CSGetConstantBuffers1(0,1,&cb,&first,&constants);else c->CSGetConstantBuffers(0,1,&cb);
    }
    ~State(){UINT keep=UINT(-1);auto* u=uav.Get();auto* b=cb.Get();c->CSSetShader(shader.Get(),classes.data(),count);c->CSSetShaderResources(0,4,srvs.data());c->CSSetUnorderedAccessViews(0,1,&u,&keep);
        if(c1)c1->CSSetConstantBuffers1(0,1,&b,&first,&constants);else c->CSSetConstantBuffers(0,1,&b);
        for(auto* s:srvs)if(s)s->Release();for(UINT i=0;i<count;++i)if(classes[i])classes[i]->Release();}
};
}
CameraGpu::CameraGpu(ID3D11Device* device,CameraLayout layout):device_(device),layout_(layout) {
    if(!device)throw std::runtime_error("Missing camera GPU device");
    ComPtr<ID3DBlob> code,errors;auto hr=D3DCompile(camera_shader,std::strlen(camera_shader),"OwnedCameraMatrices",nullptr,nullptr,"cs","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr) && errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    ok(hr,"Compile camera GPU");ok(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader_),"Create camera GPU shader");
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ok(device->CreateBuffer(&cb,nullptr,&constants_),"Create camera GPU layout");
    D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=4;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;
    texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
    ok(device->CreateTexture2D(&texture,nullptr,&output_),"Create camera matrix texture");
    ok(device->CreateUnorderedAccessView(output_.Get(),nullptr,&target_),"Create camera matrix UAV");
    ok(device->CreateShaderResourceView(output_.Get(),nullptr,&view_),"Create camera matrix SRV");
}
void CameraGpu::capture(ID3D11DeviceContext* context) {
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)throw std::runtime_error("Immediate camera GPU context required");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);if(device.Get()!=device_.Get())throw std::runtime_error("Camera GPU context changed");
    ComPtr<ID3D11DeviceContext1> c1;context->QueryInterface(IID_PPV_ARGS(&c1));
    const std::array<CameraLocation,4> locations{layout_.view,layout_.projection,layout_.world_vp,layout_.relative_vp};
    std::array<uint32_t,8> parameters{};
    for(size_t i=0;i<4;++i) {
        const auto& l=locations[i];if(l.slot>13)throw std::runtime_error("Invalid camera GPU slot");
        ComPtr<ID3D11Buffer> source;UINT first=0,count=4096;
        if(c1)c1->VSGetConstantBuffers1(l.slot,1,&source,&first,&count);else context->VSGetConstantBuffers(l.slot,1,&source);
        if(!source)throw std::runtime_error("Camera GPU binding missing");
        D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
        if(desc.ByteWidth>65536 || !desc.ByteWidth || desc.ByteWidth%16 || first>4096 || count>4096 || l.offset%16
            || l.offset<first*16 || uint64_t(l.offset)+64>desc.ByteWidth || uint64_t(l.offset)+64>uint64_t(first+count)*16)
            throw std::runtime_error("Camera GPU location outside bound range");
        D3D11_BUFFER_DESC previous{};if(copies_[i])copies_[i]->GetDesc(&previous);
        if(!copies_[i] || previous.ByteWidth!=desc.ByteWidth) {
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.StructureByteStride=0;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            inputs_[i].Reset();copies_[i].Reset();ok(device_->CreateBuffer(&desc,nullptr,&copies_[i]),"Create owned camera buffer");
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;view.BufferEx.NumElements=desc.ByteWidth/4;view.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
            ok(device_->CreateShaderResourceView(copies_[i].Get(),&view,&inputs_[i]),"Create camera raw SRV");
        }
        context->CopyResource(copies_[i].Get(),source.Get());parameters[i]=l.offset;parameters[i+4]=l.column_major;
    }
    State state(context);context->UpdateSubresource(constants_.Get(),0,nullptr,parameters.data(),0,0);
    auto* cb=constants_.Get();auto* uav=target_.Get();UINT keep=UINT(-1);std::array<ID3D11ShaderResourceView*,4> inputs{};for(size_t i=0;i<4;++i)inputs[i]=inputs_[i].Get();
    context->CSSetShader(shader_.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,&cb);context->CSSetShaderResources(0,4,inputs.data());context->CSSetUnorderedAccessViews(0,1,&uav,&keep);context->Dispatch(1,1,1);
    std::array<ID3D11ShaderResourceView*,4> none{};context->CSSetShaderResources(0,4,none.data());ID3D11UnorderedAccessView* empty=nullptr;context->CSSetUnorderedAccessViews(0,1,&empty,&keep);
}
}
