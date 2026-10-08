#include "camera_readback.hpp"
#include "compositor.hpp"
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

using namespace cc;
void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        const bool debug=SUCCEEDED(hr);
        if(FAILED(hr))check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Camera hardware device");
        ComPtr<ID3D11InfoQueue> info;device.As(&info);
        CameraReadback readback(device.Get(),{{3,256,false},{3,320,false},{3,384,false},{3,448,false}});
        std::array<uint32_t,256> data{};for(size_t i=0;i<data.size();++i)data[i]=uint32_t(i)*13+9;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(data);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> source;
        check(device->CreateBuffer(&desc,&initial,&source),"Camera source buffer");
        auto* raw=source.Get();ComPtr<ID3D11DeviceContext1> c1;context.As(&c1);const UINT first=16,count=16;
        if(c1)c1->VSSetConstantBuffers1(3,1,&raw,&first,&count);else context->VSSetConstantBuffers(3,1,&raw);
        const std::array<double,6> viewport{0,0,160,100,0,.95};
        for(uint64_t i=1;i<=3;++i)require(readback.enqueue(context.Get(),i,int64_t(i*100),viewport),"Available slot rejected");
        require(readback.pending()==3 && !readback.enqueue(context.Get(),4,400,viewport),"Three-slot bound");
        ComPtr<ID3D11Buffer> bound;context->VSGetConstantBuffers(3,1,&bound);
        require(bound.Get()==source.Get(),"Source binding changed");bound.Reset();
        std::array<uint32_t,256> changed{};context->UpdateSubresource(source.Get(),0,nullptr,changed.data(),0,0);
        context->ClearState();source.Reset();context->Flush(); // Test only; never in production readback.
        size_t n=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(readback.pending() && std::chrono::steady_clock::now()<deadline) {
            if(auto sample=readback.poll(context.Get())) {
                require(sample->sequence>=1 && sample->sequence<=3 && sample->captured_ns==int64_t(sample->sequence*100),"Timing identity lost");
                require(sample->viewport==viewport && sample->copies.size()==1,"Viewport or duplicate binding copy");
                const auto& b=sample->copies[0];require(b.slot==3 && b.bytes.size()==sizeof(data)
                    && !std::memcmp(b.bytes.data(),data.data(),sizeof(data)),"Source mutation leaked into snapshot");
                if(c1)require(b.first==first && b.count==count,"Partial range lost");
                ++n;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(n==3 && readback.pending()==0 && !readback.poll(context.Get()),"Completion/empty ring");
        if(info)for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T length=0;info->GetMessage(i,nullptr,&length);std::vector<uint8_t> bytes(length);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());info->GetMessage(i,message,&length);
            require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,message->pDescription);
        }
        std::cout<<"Camera GPU readback passed; debug layer="<<debug<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
