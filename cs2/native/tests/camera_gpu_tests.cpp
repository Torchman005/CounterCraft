#include "camera_gpu.hpp"
#include "compositor.hpp"
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <cstring>
#include <iostream>

using namespace cc;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    const bool debug=SUCCEEDED(hr);if(FAILED(hr))check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Camera GPU device");
    ComPtr<ID3D11InfoQueue> info;device.As(&info);
    CameraGpu camera(device.Get(),{{3,256,false},{3,320,true},{3,384,false},{3,448,true}});
    std::array<float,256> values{};for(size_t i=0;i<values.size();++i)values[i]=float(i)*.5f+1;
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(values);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{values.data(),0,0};
    ComPtr<ID3D11Buffer> source,prior;check(device->CreateBuffer(&desc,&initial,&source),"Camera source");check(device->CreateBuffer(&desc,&initial,&prior),"Prior CS constant");
    auto* src=source.Get();auto* cb=prior.Get();ComPtr<ID3D11DeviceContext1> c1;context.As(&c1);const UINT first=16,count=16;
    if(c1)c1->VSSetConstantBuffers1(3,1,&src,&first,&count);else context->VSSetConstantBuffers(3,1,&src);
    if(c1)c1->CSSetConstantBuffers1(0,1,&cb,&first,&count);else context->CSSetConstantBuffers(0,1,&cb);
    auto sentry=texture(device.Get(),4,4,DXGI_FORMAT_R8G8B8A8_UNORM);std::array<ID3D11ShaderResourceView*,4> old{};old.fill(sentry.srv.Get());context->CSSetShaderResources(0,4,old.data());
    camera.capture(context.Get());ComPtr<ID3D11Buffer> restored;context->CSGetConstantBuffers(0,1,&restored);require(restored.Get()==prior.Get(),"CS constants changed");
    if(c1){UINT actual_first{},actual_count{};c1->CSGetConstantBuffers1(0,1,&restored,&actual_first,&actual_count);require(actual_first==first && actual_count==count,"CS partial constant range changed");}
    for(UINT i=0;i<4;++i){ComPtr<ID3D11ShaderResourceView> view;context->CSGetShaderResources(i,1,&view);require(view.Get()==sentry.srv.Get(),"CS SRV binding changed");}
    std::array<float,256> mutated{};context->UpdateSubresource(source.Get(),0,nullptr,mutated.data(),0,0);context->ClearState();source.Reset();
    ComPtr<ID3D11Resource> resource;camera.view()->GetResource(&resource);ComPtr<ID3D11Texture2D> output;resource.As(&output);
    D3D11_TEXTURE2D_DESC read{};output->GetDesc(&read);read.Usage=D3D11_USAGE_STAGING;read.BindFlags=0;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&read,nullptr,&staging),"Camera test staging");context->CopyResource(staging.Get(),output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Oracle-only blocking camera map");
    for(size_t m=0;m<4;++m)for(size_t r=0;r<4;++r)for(size_t k=0;k<4;++k){float actual;std::memcpy(&actual,static_cast<uint8_t*>(mapped.pData)+m*mapped.RowPitch+(r*4+k)*4,4);
        const auto expected=values[64+m*16+(m%2?k*4+r:r*4+k)];require(actual==expected,"GPU matrix extraction/layout/ownership differs");}
    context->Unmap(staging.Get(),0);
    bool refused=false;try{camera.capture(context.Get());}catch(const std::exception&){refused=true;}require(refused,"Missing binding accepted");
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<uint8_t> bytes(length);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());info->GetMessage(i,message,&length);require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,message->pDescription);}
    std::cout<<"Current-frame owned camera texture, row/column layouts, source mutation/release, CS restore passed; debug="<<debug<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
