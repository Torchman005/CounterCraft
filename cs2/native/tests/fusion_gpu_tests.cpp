#include "compositor.hpp"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>

using namespace cc;
int main(){try {
    std::ifstream input(CC_FUSION_SOURCE);std::stringstream source;source<<input.rdbuf();
    if(source.str().empty())throw std::runtime_error("Missing actual effect include");
    source<<R"HLSL(
cbuffer Cases : register(b0){float4 cases[138];}
float4 vs(uint id:SV_VertexID):SV_Position {
    float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
float4 ps(float4 p:SV_Position):SV_Target {
    uint i=uint(p.x)*6;
    if(uint(p.x)>=16 && uint(p.x)<20)return CCMatrixMatches(cases[i],cases[i+1],int(cases[i+2].x))?float4(1,0,0,1):float4(0,0,1,1);
    bool visible=CCGuestVisible(cases[i].xy,cases[i].zw,cases[i+1].x,cases[i+2],cases[i+3],cases[i+4],cases[i+5].x!=0,cases[i+5].y!=0);
    return visible?float4(1,0,0,1):float4(0,0,1,1);
})HLSL";
    const auto code=source.str();auto compile=[&](const char* entry,const char* profile) {
        ComPtr<ID3DBlob> blob,errors;auto hr=D3DCompile(code.data(),code.size(),"ActualWorldFusion",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&errors);
        if(FAILED(hr) && errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        check(hr,"Compile actual fusion shader");return blob;};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Fusion hardware device");
    auto v=compile("vs","vs_5_0"),p=compile("ps","ps_5_0");
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"Fusion VS");
    check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps),"Fusion PS");
    std::array<std::array<float,4>,138> data{};std::array<bool,23> expected{};
    // Guest n=1,f=9: depth .5625 is exactly two blocks. Host n=32,f=288:
    // depth .75 is exactly three blocks; .375 is one and a half blocks.
    for(size_t i=0;i<16;++i) {
        data[i*6]={.75f,.75f,.75f,.75f};data[i*6+1]={.5625f,0,0,0};
        data[i*6+2]={1,9,1,1};data[i*6+3]={-1.125f,-36,-1,0};
        data[i*6+4]={0,1,1,1.f/32};expected[i]=true;
    }
    auto raw=[&](size_t i,float z){data[i*6]={z,z,z,z};};
    raw(1,.375f);expected[1]=false; // host wall in front
    raw(2,1); // observed host clear
    data[3*6+1][0]=1;expected[3]=false; // guest sky
    data[4*6]={.75f,.75f,.01f,.01f};expected[4]=false; // later weapon write
    data[5*6]={.375f,.75f,.375f,.75f};expected[5]=false; // MSAA edge
    data[6*6+4]={.1f,.9f,1,1.f/32};raw(6,.7f); // viewport-normalized distance
    data[7*6+4]={.1f,.9f,1,1.f/32};raw(7,.4f);expected[7]=false;
    raw(8,.25f);data[8*6+3]={.125f,36,-1,0};data[8*6+5][0]=1; // reversed host
    raw(9,.625f);data[9*6+3]={.125f,36,-1,0};data[9*6+5][0]=1;expected[9]=false;
    raw(10,.99f);data[10*6+4][1]=.8f;expected[10]=false; // unconfirmed range
    data[11*6+1][0]=-1;expected[11]=false;
    data[12*6+1][0]=std::numeric_limits<float>::quiet_NaN();expected[12]=false;
    raw(13,std::numeric_limits<float>::quiet_NaN());expected[13]=false;
    raw(14,.5625f);expected[14]=false; // equal depth keeps host
    raw(15,0);expected[15]=false; // near plane blocks farther guest
    data[16*6]=data[16*6+1]={1,2,3,4};expected[16]=true; // current rays equal
    data[17*6]={1,2,3,4.01f};data[17*6+1]={1,2,3,4};expected[17]=false; // new camera
    data[18*6]={1,2,3,4000.001f};data[18*6+1]={1,2,3,4000};data[18*6+2][0]=2;expected[18]=true; // product FP tolerance
    data[19*6]={1,2,std::numeric_limits<float>::infinity(),4};data[19*6+1]={1,2,3,4};expected[19]=false;
    for(size_t i=20;i<23;++i)for(size_t r=0;r<6;++r)data[i*6+r]=data[r];
    data[20*6]={.75f,.75f,1,1};data[20*6+5][1]=1;expected[20]=true;
    data[21*6]={.375f,.375f,1,1};data[21*6+5][1]=1;expected[21]=false;
    data[22*6]={.75f,.75f,1,1};expected[22]=false;
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(data);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&desc,&initial,&buffer),"Fusion oracle cases");auto* b=buffer.Get();context->PSSetConstantBuffers(0,1,&b);
    auto target=texture(device.Get(),23,1,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);auto* rtv=target.rtv.Get();
    context->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT viewport{0,0,23,1,0,1};context->RSSetViewports(1,&viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->Draw(3,0);
    context->OMSetRenderTargets(0,nullptr,nullptr);const auto rgba=read_rgba(device.Get(),context.Get(),target.texture.Get());
    for(size_t i=0;i<expected.size();++i)if((rgba[i*4]==255)!=expected[i] || rgba[i*4+2]!=(expected[i]?0:255))throw std::runtime_error("Fusion GPU case "+std::to_string(i)+" failed");
    // Mixed full-client payloads must never enter world compositors.
    Frame mixed;mixed.metadata.full_client=true;Compositor compositor(device.Get(),context.Get());
    bool refused=false;try{compositor.upload(mixed);}catch(const std::exception&){refused=true;}
    if(!refused)throw std::runtime_error("Mixed client layer admitted");
    std::cout<<"Actual world effect GPU: 23 occlusion, camera and observed late-clear cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
