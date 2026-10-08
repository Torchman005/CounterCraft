#include "depth_readback.hpp"
#include <d3d11_1.h>
#include <cstring>
#include <stdexcept>

namespace cc {
using Microsoft::WRL::ComPtr;
namespace {
void ok(HRESULT hr,const char* why) { if(FAILED(hr)) throw std::runtime_error(why); }
void validate(ID3D11DeviceContext* context,const DepthSampler& expected) {
    if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) throw std::runtime_error("Immediate readback context required");
    ComPtr<ID3D11Device> actual; context->GetDevice(&actual);
    if(!expected.same_device(actual.Get())) throw std::runtime_error("Readback context device mismatch");
}
struct Mapping {
    ID3D11DeviceContext* context;
    ID3D11Resource* resource;
    D3D11_MAPPED_SUBRESOURCE data{};
    bool mapped{};
    Mapping(ID3D11DeviceContext* c,ID3D11Resource* r):context(c),resource(r) {
        const auto hr=c->Map(r,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&data);
        if(hr!=DXGI_ERROR_WAS_STILL_DRAWING) ok(hr,"Nonblocking capture map");
        mapped=SUCCEEDED(hr);
    }
    ~Mapping() { if(mapped) context->Unmap(resource,0); }
};
}
DepthReadback::DepthReadback(ID3D11Device* device):device_(device),sampler_(device) {}
size_t DepthReadback::pending() const { size_t n=0; for(const auto& s:slots_) n+=s.busy; return n; }
bool DepthReadback::enqueue(ID3D11DeviceContext* context,ID3D11Texture2D* source,nlohmann::json metadata) {
    validate(context,sampler_);
    Slot* free=nullptr; for(auto& s:slots_) if(!s.busy) { free=&s; break; }
    if(!free) return false;
    if(!source) throw std::runtime_error("Capture depth is null");
    D3D11_TEXTURE2D_DESC desc; source->GetDesc(&desc); sampler_.prepare(desc);
    // Build the whole slot before issuing the completion marker. Exception paths
    // release local owned resources and never publish a partially queued capture.
    Slot slot; slot.metadata=std::move(metadata); slot.width=desc.Width; slot.height=desc.Height;
    slot.metadata["depth"]={{"size",{desc.Width,desc.Height}},{"sourceFormat",uint32_t(desc.Format)},
        {"samples",desc.SampleDesc.Count},{"quality",desc.SampleDesc.Quality},
        {"file","depth-minmax.f32"},{"encoding","little-endian float32 min,max; top-left row-major"}};
    D3D11_TEXTURE2D_DESC staging; sampler_.output()->GetDesc(&staging);
    staging.Usage=D3D11_USAGE_STAGING; staging.BindFlags=0; staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ok(device_->CreateTexture2D(&staging,nullptr,&slot.depth),"Create capture staging depth");
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0}; ok(device_->CreateQuery(&query,&slot.ready),"Create capture completion marker");
    ComPtr<ID3D11DeviceContext1> c1; context->QueryInterface(IID_PPV_ARGS(&c1));
    slot.metadata["constantBuffers"]=nlohmann::json::array();
    for(bool vertex:{true,false}) {
        std::array<ComPtr<ID3D11Buffer>,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> owners;
        std::array<ID3D11Buffer*,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};
        std::array<UINT,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> first{},count{};
        const UINT n=UINT(buffers.size());
        if(c1) {
            if(vertex) c1->VSGetConstantBuffers1(0,n,buffers.data(),first.data(),count.data());
            else c1->PSGetConstantBuffers1(0,n,buffers.data(),first.data(),count.data());
        } else {
            if(vertex) context->VSGetConstantBuffers(0,n,buffers.data());
            else context->PSGetConstantBuffers(0,n,buffers.data());
        }
        for(UINT i=0;i<n;++i) owners[i].Attach(buffers[i]);
        for(UINT i=0;i<n;++i) {
            if(!owners[i]) continue;
            D3D11_BUFFER_DESC original; owners[i]->GetDesc(&original);
            // D3D11.1 can allocate larger buffers; bound the diagnostic copy.
            // A skipped buffer is explicit, never represented as complete evidence.
            if(original.ByteWidth>65536) {
                slot.metadata["constantBuffers"].push_back({{"stage",vertex?"VS":"PS"},{"slot",i},
                    {"bytes",original.ByteWidth},{"skipped","over-64KiB"}}); continue;
            }
            Buffer b; b.name=std::string(vertex?"vs-":"ps-")+std::to_string(i)+".bin"; b.bytes=original.ByteWidth;
            D3D11_BUFFER_DESC read{}; read.ByteWidth=b.bytes; read.Usage=D3D11_USAGE_STAGING; read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ok(device_->CreateBuffer(&read,nullptr,&b.staging),"Create capture staging constant buffer");
            context->CopyResource(b.staging.Get(),owners[i].Get());
            slot.metadata["constantBuffers"].push_back({{"stage",vertex?"VS":"PS"},{"slot",i},
                {"bytes",b.bytes},{"firstConstant",c1?nlohmann::json(first[i]):nlohmann::json(nullptr)},
                {"numConstants",c1?nlohmann::json(count[i]):nlohmann::json(nullptr)},{"file",b.name}});
            slot.buffers.push_back(std::move(b));
        }
    }
    sampler_.sample(context,source); context->CopyResource(slot.depth.Get(),sampler_.output());
    context->End(slot.ready.Get()); slot.busy=true; *free=std::move(slot); return true;
}
std::optional<DepthCaptureFrame> DepthReadback::poll(ID3D11DeviceContext* context) {
    validate(context,sampler_);
    for(auto& s:slots_) {
        if(!s.busy) continue;
        BOOL completed=FALSE;
        const auto hr=context->GetData(s.ready.Get(),&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        ok(hr,"Poll capture completion"); if(hr==S_FALSE || !completed) continue;
        // Even after the event completes, refuse to block if the driver still
        // reports a busy mapping. A later frame retries the intact slot.
        Mapping depth(context,s.depth.Get()); if(!depth.mapped) continue;
        DepthCaptureFrame result; result.metadata=s.metadata;
        CaptureBytes pixels{"depth-minmax.f32",std::vector<uint8_t>(size_t(s.width)*s.height*8)};
        for(UINT y=0;y<s.height;++y) std::memcpy(pixels.bytes.data()+size_t(y)*s.width*8,
            static_cast<const uint8_t*>(depth.data.pData)+size_t(y)*depth.data.RowPitch,size_t(s.width)*8);
        result.files.push_back(std::move(pixels));
        bool ready=true;
        for(const auto& b:s.buffers) {
            Mapping map(context,b.staging.Get()); if(!map.mapped) { ready=false; break; }
            CaptureBytes bytes{b.name,std::vector<uint8_t>(b.bytes)};
            std::memcpy(bytes.bytes.data(),map.data.pData,b.bytes); result.files.push_back(std::move(bytes));
        }
        if(!ready) continue;
        s.busy=false; return result;
    }
    return std::nullopt;
}
}
