#include "camera_readback.hpp"
#include <d3d11_1.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace cc {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr,const char* why) {if(FAILED(hr)) throw std::runtime_error(why);}
}
HostCamera CameraSample::decode(const CameraLayout& layout) const {
    std::vector<CameraBuffer> buffers;
    for(const auto& b:copies) buffers.push_back({b.slot,b.first,b.count,b.bytes});
    return decode_host_camera(layout,buffers,viewport);
}
CameraReadback::CameraReadback(ID3D11Device* device,CameraLayout layout):device_(device),layout_(layout) {
    if(!device) throw std::runtime_error("Null camera device");
}
void CameraReadback::validate(ID3D11DeviceContext* context) const {
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) throw std::runtime_error("Immediate camera context required");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    if(device.Get()!=device_.Get()) throw std::runtime_error("Camera context changed devices");
}
size_t CameraReadback::pending() const {size_t n=0;for(const auto& s:slots_)n+=s.busy;return n;}
bool CameraReadback::enqueue(ID3D11DeviceContext* context,uint64_t sequence,int64_t captured_ns,const std::array<double,6>& viewport) {
    validate(context);
    Slot* available=nullptr; for(auto& s:slots_) if(!s.busy) {available=&s;break;}
    if(!available) return false;
    auto& s=*available;
    std::vector<uint32_t> slots;
    for(const auto& location:{layout_.view,layout_.projection,layout_.world_vp,layout_.relative_vp}) {
        if(location.slot>13) throw std::runtime_error("Invalid camera slot");
        if(std::find(slots.begin(),slots.end(),location.slot)==slots.end())slots.push_back(location.slot);
    }
    ComPtr<ID3D11DeviceContext1> c1;context->QueryInterface(IID_PPV_ARGS(&c1));
    if(s.buffers.size()!=slots.size())s.buffers.resize(slots.size());
    for(size_t i=0;i<slots.size();++i) {
        auto& b=s.buffers[i];ComPtr<ID3D11Buffer> source;
        UINT first=0,count=4096;
        if(c1)c1->VSGetConstantBuffers1(slots[i],1,&source,&first,&count);
        else context->VSGetConstantBuffers(slots[i],1,&source);
        if(!source)throw std::runtime_error("Calibrated VS binding missing");
        D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
        if(!desc.ByteWidth || desc.ByteWidth>65536 || desc.ByteWidth%16 || first>4096 || count>4096)
            throw std::runtime_error("Calibrated VS binding exceeds bounds");
        if(!b.staging || b.bytes!=desc.ByteWidth) {
            D3D11_BUFFER_DESC read{};read.ByteWidth=desc.ByteWidth;read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            b.staging.Reset();ok(device_->CreateBuffer(&read,nullptr,&b.staging),"Create camera staging");
        }
        b.slot=slots[i];b.bytes=desc.ByteWidth;b.first=first;b.count=count;
        context->CopyResource(b.staging.Get(),source.Get());
    }
    if(!s.ready){const D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};ok(device_->CreateQuery(&desc,&s.ready),"Create camera query");}
    s.sequence=sequence;s.captured_ns=captured_ns;s.viewport=viewport;
    context->End(s.ready.Get());s.busy=true;return true;
}
std::optional<CameraSample> CameraReadback::poll(ID3D11DeviceContext* context) {
    validate(context);
    for(auto& s:slots_) {
        if(!s.busy)continue;
        BOOL ready=FALSE;
        const auto hr=context->GetData(s.ready.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        ok(hr,"Poll camera query");if(hr==S_FALSE || !ready)continue;
        CameraSample result{s.sequence,s.captured_ns,s.viewport,{}};
        bool complete=true;
        for(const auto& b:s.buffers) {
            CameraCopy copy{b.slot,b.first,b.count,std::vector<uint8_t>(b.bytes)};
            D3D11_MAPPED_SUBRESOURCE data{};
            const auto mapped=context->Map(b.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&data);
            if(mapped==DXGI_ERROR_WAS_STILL_DRAWING){complete=false;break;}
            ok(mapped,"Map camera staging");
            std::memcpy(copy.bytes.data(),data.pData,b.bytes);context->Unmap(b.staging.Get(),0);
            result.copies.push_back(std::move(copy));
        }
        if(!complete)continue;
        s.busy=false;return result;
    }
    return std::nullopt;
}
}
