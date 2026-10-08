#include "depth_readback.hpp"
#include "compositor.hpp"
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

using namespace cc;
namespace { void require(bool v,const char* why) { if(!v) throw std::runtime_error(why); } }
int main() {
    try {
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level;
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        const bool debug=SUCCEEDED(hr);
        if(FAILED(hr)) check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
            nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Readback hardware device");
        ComPtr<ID3D11InfoQueue> info; device.As(&info);
        DepthReadback readback(device.Get());
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=37; desc.Height=19; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_D32_FLOAT; desc.SampleDesc={4,0}; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> source; check(device->CreateTexture2D(&desc,nullptr,&source),"Readback source");
        ComPtr<ID3D11DepthStencilView> dsv; check(device->CreateDepthStencilView(source.Get(),nullptr,&dsv),"Readback DSV");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=1024; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.Usage=D3D11_USAGE_DEFAULT;
        std::array<uint32_t,256> payload{};
        for(size_t i=0;i<payload.size();++i) payload[i]=uint32_t(i)*17+3;
        D3D11_SUBRESOURCE_DATA initial{payload.data(),0,0}; ComPtr<ID3D11Buffer> buffer;
        check(device->CreateBuffer(&cb,&initial,&buffer),"Readback constants");
        auto* raw=buffer.Get(); ComPtr<ID3D11DeviceContext1> c1; context.As(&c1);
        const UINT first=16,count=16;
        if(c1) c1->VSSetConstantBuffers1(3,1,&raw,&first,&count); else context->VSSetConstantBuffers(3,1,&raw);
        context->PSSetConstantBuffers(7,1,&raw);
        context->OMSetRenderTargets(0,nullptr,dsv.Get());
        for(size_t i=0;i<DepthReadback::capacity;++i) {
            context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.25f+float(i)*.125f,0);
            require(readback.enqueue(context.Get(),source.Get(),{{"id",i}}),"Queue rejected available slot");
        }
        require(readback.pending()==3,"Ring pending count");
        require(!readback.enqueue(context.Get(),source.Get(),{{"id",99}}),"Ring accepted overflow");
        // Change and release host data before the later-frame poll. The copies
        // must retain each enqueue's bytes, not the current host contents.
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.9f,0);
        std::array<uint32_t,256> changed{}; context->UpdateSubresource(buffer.Get(),0,nullptr,changed.data(),0,0);
        context->ClearState(); buffer.Reset(); source.Reset(); dsv.Reset();
        context->Flush(); // Independent test oracle only. Production never flushes.
        size_t completed=0; const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(readback.pending() && std::chrono::steady_clock::now()<deadline) {
            if(auto frame=readback.poll(context.Get())) {
                const auto id=frame->metadata.at("id").get<unsigned>(); require(id<3,"Capture identity changed");
                const auto& bytes=frame->files.at(0).bytes; require(bytes.size()==37*19*8,"Depth row-pitch normalization");
                for(size_t offset=0;offset<bytes.size();offset+=4) {
                    float depth=0; std::memcpy(&depth,bytes.data()+offset,4);
                    require(std::abs(depth-(.25f+id*.125f))<1e-6,"Depth changed after host reuse");
                }
                require(frame->files.size()==3,"Bound VS/PS copies missing");
                for(size_t i=1;i<frame->files.size();++i) require(frame->files[i].bytes.size()==sizeof(payload)
                    && std::memcmp(frame->files[i].bytes.data(),payload.data(),sizeof(payload))==0,"Bound constants changed after host update/release");
                for(const auto& b:frame->metadata.at("constantBuffers")) if(b.at("stage")=="VS" && c1) {
                    require(b.at("slot")==3 && b.at("firstConstant")==first && b.at("numConstants")==count,"Partial binding range lost");
                }
                ++completed;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completed==3 && readback.pending()==0,"Nonblocking ring failed to complete");
        require(!readback.poll(context.Get()),"Empty ring returned duplicate capture");
        if(info) for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T size=0; info->GetMessage(i,nullptr,&size); std::vector<uint8_t> bytes(size);
            auto* m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data()); info->GetMessage(i,m,&size);
            require(m->Severity>D3D11_MESSAGE_SEVERITY_WARNING,m->pDescription);
        }
        std::cout<<"{\"depthReadbackTests\":\"passed\",\"hardware\":true,\"debugLayer\":"<<(debug?"true":"false")
            <<",\"captures\":"<<completed<<",\"partialBindings\":"<<(c1?"true":"false")<<"}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
