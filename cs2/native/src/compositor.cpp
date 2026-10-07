#include "compositor.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace cc {
namespace {
const char* shader = R"HLSL(
cbuffer Params : register(b0) {
    float4 planes; // guest near/far, host near/far
    uint4 sizes;   // guest width/height, output width/height
    uint4 flags;   // reversed host Z, guest active
};
Texture2D<float4> hostColor : register(t0);
Texture2D<float> hostDepth : register(t1);
Texture2D<float4> guestColor : register(t2);
Texture2D<float> guestDepth : register(t3);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
}
float linearDepth(float depth, float n, float f) { return n * f / (f - depth * (f - n)); }
float4 ps(float4 position : SV_Position) : SV_Target {
    int2 hostPixel = int2(position.xy);
    float4 host = hostColor.Load(int3(hostPixel,0));
    if (!flags.y) return host;
    int2 guestPixel = min(int2(position.xy * float2(sizes.xy) / float2(sizes.zw)),int2(sizes.xy)-1);
    guestPixel.y = int(sizes.y) - 1 - guestPixel.y; // OpenGL bottom row -> D3D top row.
    float guestZ = guestDepth.Load(int3(guestPixel,0));
    if (!isfinite(guestZ) || guestZ < 0 || guestZ >= 1) return host; // Guest sky is not geometry.
    float hostZ = hostDepth.Load(int3(hostPixel,0));
    if (!isfinite(hostZ) || hostZ < 0 || hostZ > 1) return host;
    if (flags.x) hostZ = 1 - hostZ;
    float guestDistance = linearDepth(guestZ,planes.x,planes.y);
    float hostDistance = linearDepth(hostZ,planes.z,planes.w);
    return guestDistance < hostDistance ? guestColor.Load(int3(guestPixel,0)) : host;
}
)HLSL";
ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    HRESULT result = D3DCompile(shader,std::strlen(shader),"CounterCraftCompositor",nullptr,nullptr,entry,target,
                                D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if (FAILED(result) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    check(result,"Compile compositor shader");
    return code;
}
}
void check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": HRESULT " + std::to_string(uint32_t(result)));
}
Texture texture(ID3D11Device* device, int width, int height, DXGI_FORMAT format, const void* data, bool target) {
    if (width < 1 || height < 1 || uint64_t(width) * height > max_pixels) throw std::runtime_error("Texture dimensions");
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = UINT(width); desc.Height = UINT(height); desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (target ? D3D11_BIND_RENDER_TARGET : 0);
    D3D11_SUBRESOURCE_DATA initial{data,UINT(width * 4),0};
    Texture result;
    check(device->CreateTexture2D(&desc,data ? &initial : nullptr,&result.texture),"Create frame texture");
    check(device->CreateShaderResourceView(result.texture.Get(),nullptr,&result.srv),"Create frame SRV");
    if (target) check(device->CreateRenderTargetView(result.texture.Get(),nullptr,&result.rtv),"Create frame RTV");
    return result;
}
Compositor::Compositor(ID3D11Device* device, ID3D11DeviceContext* context) : device_(device), context_(context) {
    auto vs = compile("vs","vs_5_0"), ps = compile("ps","ps_5_0");
    check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_),"Create compositor VS");
    check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_),"Create compositor PS");
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = 48; buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&buffer,nullptr,&constants_),"Create compositor constants");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = FALSE;
    check(device->CreateDepthStencilState(&depth,&no_depth_),"Create compositor depth state");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    check(device->CreateRasterizerState(&raster,&raster_),"Create compositor raster state");
}
void Compositor::upload(const Frame& frame) {
    if (frame.pixels.size() != size_t(frame.metadata.width) * frame.metadata.height * 8)
        throw std::runtime_error("Incomplete GPU upload payload");
    if (!color_.texture || metadata_.width != frame.metadata.width || metadata_.height != frame.metadata.height) {
        color_ = texture(device_.Get(),frame.metadata.width,frame.metadata.height,DXGI_FORMAT_R8G8B8A8_UNORM);
        depth_ = texture(device_.Get(),frame.metadata.width,frame.metadata.height,DXGI_FORMAT_R32_FLOAT);
    }
    metadata_ = frame.metadata;
    context_->UpdateSubresource(color_.texture.Get(),0,nullptr,frame.rgba().data(),UINT(metadata_.width*4),0);
    context_->UpdateSubresource(depth_.texture.Get(),0,nullptr,frame.depth().data(),UINT(metadata_.width*4),0);
}
void Compositor::draw(ID3D11ShaderResourceView* host_color, ID3D11ShaderResourceView* host_depth,
                     ID3D11RenderTargetView* output, int width, int height, float hn, float hf, bool reversed, bool active) {
    if (!host_color || !host_depth || !output || width < 1 || height < 1 || hn <= 0 || hf <= hn)
        throw std::runtime_error("Invalid host composition inputs");
    struct Constants { float planes[4]; uint32_t sizes[4], flags[4]; } parameters{
        {float(metadata_.near_plane),float(metadata_.far_plane),hn,hf},
        {uint32_t(metadata_.width),uint32_t(metadata_.height),uint32_t(width),uint32_t(height)},
        {uint32_t(reversed),uint32_t(active && bool(color_.texture)),0,0}};
    context_->UpdateSubresource(constants_.Get(),0,nullptr,&parameters,0,0);
    ID3D11Buffer* cb = constants_.Get(); context_->PSSetConstantBuffers(0,1,&cb);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.Get(),nullptr,0); context_->PSSetShader(pixel_.Get(),nullptr,0);
    context_->GSSetShader(nullptr,nullptr,0); context_->HSSetShader(nullptr,nullptr,0); context_->DSSetShader(nullptr,nullptr,0);
    context_->RSSetState(raster_.Get()); D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1}; context_->RSSetViewports(1,&viewport);
    context_->OMSetRenderTargets(1,&output,nullptr); context_->OMSetDepthStencilState(no_depth_.Get(),0);
    context_->OMSetBlendState(nullptr,nullptr,UINT_MAX);
    ID3D11ShaderResourceView* sources[]{host_color,host_depth,color_.srv.Get(),depth_.srv.Get()};
    context_->PSSetShaderResources(0,4,sources); context_->Draw(3,0);
    ID3D11ShaderResourceView* empty[4]{}; context_->PSSetShaderResources(0,4,empty);
    context_->OMSetRenderTargets(0,nullptr,nullptr);
}
void Compositor::clear() { color_ = {}; depth_ = {}; metadata_ = {}; }
std::vector<uint8_t> read_rgba(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.SampleDesc.Count != 1) throw std::runtime_error("Readback format");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; check(device->CreateTexture2D(&desc,nullptr,&staging),"Create diagnostic readback");
    context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map diagnostic readback");
    std::vector<uint8_t> pixels(size_t(desc.Width)*desc.Height*4);
    for (UINT y=0;y<desc.Height;++y) std::memcpy(pixels.data()+size_t(y)*desc.Width*4,static_cast<uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,desc.Width*4);
    context->Unmap(staging.Get(),0);
    return pixels;
}
void save_bmp(const std::filesystem::path& path,int width,int height,std::span<const uint8_t> rgba) {
    if (rgba.size()!=size_t(width)*height*4) throw std::runtime_error("BMP size");
    uint32_t stride = (uint32_t(width)*3+3)&~3u, image_size = stride*uint32_t(height);
    std::vector<uint8_t> bytes(54+image_size);
    auto put = [&](size_t offset,uint32_t n,size_t count=4) { for(size_t i=0;i<count;++i) bytes[offset+i]=uint8_t(n>>(i*8)); };
    bytes[0]='B'; bytes[1]='M'; put(2,uint32_t(bytes.size())); put(10,54); put(14,40); put(18,uint32_t(width)); put(22,uint32_t(height));
    put(26,1,2); put(28,24,2); put(34,image_size);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        size_t source=(size_t(height-1-y)*width+x)*4,target=54+size_t(y)*stride+size_t(x)*3;
        bytes[target]=rgba[source+2]; bytes[target+1]=rgba[source+1]; bytes[target+2]=rgba[source];
    }
    std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file) throw std::runtime_error("Cannot save diagnostic BMP");
}
}
