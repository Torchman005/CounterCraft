#include "depth_sampler.hpp"
#include "compositor.hpp"
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cc;
void require(bool v,const char* why) { if(!v) throw std::runtime_error(why); }
const char* raster=R"HLSL(
float4 vs(uint id:SV_VertexID):SV_Position {
    float2 uv=float2((id<<1)&2,id&2); return float4(uv.x*2-1,1-uv.y*2,0,1);
}
float ps(float4 p:SV_Position,uint s:SV_SampleIndex):SV_Depth {
    return (p.x<16 ? 0.2 : 0.7) + s*0.02;
})HLSL";
ComPtr<ID3DBlob> compile(const char* entry,const char* profile) {
    ComPtr<ID3DBlob> code,errors;
    check(D3DCompile(raster,std::strlen(raster),nullptr,nullptr,nullptr,entry,profile,0,0,&code,&errors),"Compile depth oracle"); return code;
}
}
int main() {
    try {
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level;
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context);
        const bool debug=SUCCEEDED(hr);
        if(FAILED(hr)) hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context);
        const bool hardware=SUCCEEDED(hr);
        if(FAILED(hr)) check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,&level,&context),"Create sampler oracle device");
        ComPtr<ID3D11InfoQueue> info; device.As(&info);
        DepthSampler sampler(device.Get());
        const char* cs_source="RWTexture2D<float> x:register(u0); [numthreads(1,1,1)] void main(uint3 i:SV_DispatchThreadID){x[i.xy]=1;}";
        ComPtr<ID3DBlob> cs_code; check(D3DCompile(cs_source,std::strlen(cs_source),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&cs_code,nullptr),"Compile sentinel CS");
        ComPtr<ID3D11ComputeShader> sentinel_shader; check(device->CreateComputeShader(cs_code->GetBufferPointer(),cs_code->GetBufferSize(),nullptr,&sentinel_shader),"Sentinel CS");
        D3D11_TEXTURE2D_DESC sentinel_desc{}; sentinel_desc.Width=sentinel_desc.Height=8; sentinel_desc.MipLevels=sentinel_desc.ArraySize=1;
        sentinel_desc.Format=DXGI_FORMAT_R32_FLOAT; sentinel_desc.SampleDesc={1,0}; sentinel_desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> sentinel_texture; check(device->CreateTexture2D(&sentinel_desc,nullptr,&sentinel_texture),"Sentinel UAV texture");
        ComPtr<ID3D11UnorderedAccessView> sentinel_uav; check(device->CreateUnorderedAccessView(sentinel_texture.Get(),nullptr,&sentinel_uav),"Sentinel UAV");
        const auto vs=compile("vs","vs_5_0"),ps=compile("ps","ps_5_0");
        ComPtr<ID3D11VertexShader> vertex; ComPtr<ID3D11PixelShader> pixel;
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Oracle VS");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Oracle PS");
        D3D11_DEPTH_STENCIL_DESC depth_state{}; depth_state.DepthEnable=TRUE;
        depth_state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; depth_state.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> depth; check(device->CreateDepthStencilState(&depth_state,&depth),"Oracle depth state");
        D3D11_RASTERIZER_DESC rs{}; rs.FillMode=D3D11_FILL_SOLID; rs.CullMode=D3D11_CULL_NONE; rs.MultisampleEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster_state; check(device->CreateRasterizerState(&rs,&raster_state),"Oracle raster state");
        unsigned cases=0,skipped=0;
        for(const auto [typed,base]:{std::pair{DXGI_FORMAT_D16_UNORM,DXGI_FORMAT_R16_TYPELESS},
            {DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_R24G8_TYPELESS},
            {DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_R32_TYPELESS},
            {DXGI_FORMAT_D32_FLOAT_S8X24_UINT,DXGI_FORMAT_R32G8X24_TYPELESS}}) {
            for(UINT n:{1u,2u,4u,8u}) for(bool typeless:{false,true}) {
                UINT quality=0; check(device->CheckMultisampleQualityLevels(typed,n,&quality),"Check oracle MSAA");
                if(!quality) { ++skipped; continue; }
                D3D11_TEXTURE2D_DESC desc{}; desc.Width=32; desc.Height=24; desc.ArraySize=desc.MipLevels=1;
                desc.Format=typeless?base:typed; desc.SampleDesc={n,0}; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
                ComPtr<ID3D11Texture2D> source; check(device->CreateTexture2D(&desc,nullptr,&source),"Oracle depth texture");
                D3D11_DEPTH_STENCIL_VIEW_DESC view{}; view.Format=typed;
                view.ViewDimension=n>1?D3D11_DSV_DIMENSION_TEXTURE2DMS:D3D11_DSV_DIMENSION_TEXTURE2D;
                ComPtr<ID3D11DepthStencilView> dsv; check(device->CreateDepthStencilView(source.Get(),&view,&dsv),"Oracle DSV");
                context->OMSetRenderTargets(0,nullptr,dsv.Get()); context->OMSetDepthStencilState(depth.Get(),0);
                context->RSSetState(raster_state.Get()); D3D11_VIEWPORT viewport{0,0,32,24,0,1}; context->RSSetViewports(1,&viewport);
                context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
                context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0); context->Draw(3,0);
                sampler.prepare(desc);
                // Preserve arbitrary CS shader/SRV/UAV bindings plus graphics DSV.
                auto sentinel=texture(device.Get(),32,24,DXGI_FORMAT_R32_FLOAT);
                auto* sentinel_srv=sentinel.srv.Get(); context->CSSetShaderResources(0,1,&sentinel_srv);
                context->CSSetShader(sentinel_shader.Get(),nullptr,0);
                auto* u=sentinel_uav.Get(); context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
                sampler.sample(context.Get(),source.Get());
                ComPtr<ID3D11ShaderResourceView> retained; context->CSGetShaderResources(0,1,&retained);
                require(retained.Get()==sentinel_srv,"Host CS SRV0 not restored");
                ComPtr<ID3D11ComputeShader> retained_shader; context->CSGetShader(&retained_shader,nullptr,nullptr);
                require(retained_shader.Get()==sentinel_shader.Get(),"Host CS shader not restored");
                ComPtr<ID3D11UnorderedAccessView> retained_uav; context->CSGetUnorderedAccessViews(0,1,&retained_uav);
                require(retained_uav.Get()==sentinel_uav.Get(),"Host CS UAV0 not restored");
                ComPtr<ID3D11DepthStencilView> retained_dsv; context->OMGetRenderTargets(0,nullptr,&retained_dsv);
                require(retained_dsv.Get()==dsv.Get(),"Host graphics depth binding changed");
                D3D11_TEXTURE2D_DESC out_desc; sampler.output()->GetDesc(&out_desc);
                out_desc.Usage=D3D11_USAGE_STAGING; out_desc.BindFlags=0; out_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging; check(device->CreateTexture2D(&out_desc,nullptr,&staging),"Oracle readback");
                context->CopyResource(staging.Get(),sampler.output()); D3D11_MAPPED_SUBRESOURCE map;
                check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"Oracle map");
                bool correct=true;
                for(UINT y=0;y<24;++y) for(UINT x=0;x<32;++x) {
                    const auto* pair=reinterpret_cast<const float*>(static_cast<const uint8_t*>(map.pData)+y*map.RowPitch)+x*2;
                    const float expected=x<16?.2f:.7f;
                    correct=correct && std::abs(pair[0]-expected)<2e-5f && std::abs(pair[1]-(expected+(n-1)*.02f))<2e-5f;
                }
                context->Unmap(staging.Get(),0); require(correct,"Raw per-sample min/max mismatch");
                ++cases;
            }
        }
        auto invalid=sampler.description(); invalid.ArraySize=2; bool refused=false;
        try { sampler.prepare(invalid); } catch(const std::exception&) { refused=true; }
        require(refused,"Unsupported arrays silently accepted");
        ComPtr<ID3D11Device> other; ComPtr<ID3D11DeviceContext> other_context;
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &other,nullptr,&other_context),"Create foreign-device rejection oracle");
        require(sampler.same_device(device.Get()) && !sampler.same_device(other.Get()),"Device identity admits foreign device");
        auto foreign_desc=sampler.description(); ComPtr<ID3D11Texture2D> foreign;
        check(other->CreateTexture2D(&foreign_desc,nullptr,&foreign),"Foreign depth source");
        refused=false; try { sampler.sample(context.Get(),foreign.Get()); } catch(const std::exception&) { refused=true; }
        require(refused,"Foreign source device not rejected");
        if(info) {
            for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
                SIZE_T bytes=0; info->GetMessage(i,nullptr,&bytes); std::vector<uint8_t> data(bytes);
                auto* message=reinterpret_cast<D3D11_MESSAGE*>(data.data()); info->GetMessage(i,message,&bytes);
                require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,"D3D11 debug-layer warning/error");
            }
        }
        context->ClearState();
        std::cout<<"{\"depthSamplerTests\":\"passed\",\"hardware\":"<<(hardware?"true":"false")
            <<",\"debugLayer\":"<<(debug?"true":"false")<<",\"cases\":"<<cases<<",\"unsupportedCases\":"<<skipped<<"}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
