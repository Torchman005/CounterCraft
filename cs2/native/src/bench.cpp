#include "compositor.hpp"
#include "receiver.hpp"
#include <d3dcompiler.h>
#include <json.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
using namespace cc;
using json=nlohmann::json;
class CubeScene {
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertices_,constants_;
    ComPtr<ID3D11DepthStencilState> depth_state_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    int width_{},height_{};
public:
    Texture color,depth;
    CubeScene(ID3D11Device* device,ID3D11DeviceContext* context):device_(device),context_(context){
        const char* source=R"(
        cbuffer Cube : register(b0) { float4 lens; float4 origin; };
        float4 vs(float3 p:POSITION):SV_Position {
            p=p*origin.w+origin.xyz;
            float z=lens.z*lens.w/(lens.w-lens.z)-p.z*lens.z/(lens.w-lens.z);
            return float4(p.x*lens.x,p.y*lens.y,z,p.z);
        }
        float4 ps():SV_Target { return float4(1,0,0,1); }
        )";
        ComPtr<ID3DBlob> vs,ps,errors;
        check(D3DCompile(source,std::strlen(source),"cube",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&errors),"Compile cube VS");
        check(D3DCompile(source,std::strlen(source),"cube",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&errors),"Compile cube PS");
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vs_),"Create cube VS");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&ps_),"Create cube PS");
        D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(device->CreateInputLayout(&element,1,vs->GetBufferPointer(),vs->GetBufferSize(),&layout_),"Create cube layout");
        const float corners[8][3]={{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                                  {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
        const int indices[36]={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,3,2,6,3,6,7,0,3,7,0,7,4,1,5,6,1,6,2};
        float vertices[108]{};for(int i=0;i<36;++i)std::copy_n(corners[indices[i]],3,vertices+i*3);
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(vertices);buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{vertices,0,0};check(device->CreateBuffer(&buffer,&initial,&vertices_),"Create cube vertices");
        buffer.ByteWidth=32;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&buffer,nullptr,&constants_),"Create cube constants");
        D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
        check(device->CreateDepthStencilState(&state,&depth_state_),"Create reversed-Z state");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&raster,&raster_),"Create cube raster state");
    }
    void draw(int width,int height,float sx,float sy,float distance,float size=1) {
        if(width_!=width||height_!=height){
            color=texture(device_,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);
            D3D11_TEXTURE2D_DESC desc{};desc.Width=UINT(width);desc.Height=UINT(height);desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
            depth={};dsv_.Reset();check(device_->CreateTexture2D(&desc,nullptr,&depth.texture),"Create host depth");
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            check(device_->CreateDepthStencilView(depth.texture.Get(),&dv,&dsv_),"Create host DSV");
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
            check(device_->CreateShaderResourceView(depth.texture.Get(),&sv,&depth.srv),"Create host depth SRV");
            width_=width;height_=height;
        }
        const float blue[]{.03f,.12f,.2f,1};context_->ClearRenderTargetView(color.rtv.Get(),blue);
        context_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH,0,0);
        float params[]{sx,sy,.15f,10000.f,0,0,distance,size};context_->UpdateSubresource(constants_.Get(),0,nullptr,params,0,0);
        ID3D11Buffer* cb=constants_.Get();context_->VSSetConstantBuffers(0,1,&cb);
        ID3D11Buffer* vb=vertices_.Get();UINT stride=12,offset=0;context_->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context_->IASetInputLayout(layout_.Get());context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
        context_->RSSetState(raster_.Get());D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context_->RSSetViewports(1,&viewport);
        context_->OMSetDepthStencilState(depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,UINT_MAX);
        ID3D11RenderTargetView* target=color.rtv.Get();context_->OMSetRenderTargets(1,&target,dsv_.Get());context_->Draw(36,0);
        context_->OMSetRenderTargets(0,nullptr,nullptr);
    }
};
Frame synthetic(int width,int height,bool sky=false){
    Frame frame;frame.metadata.width=width;frame.metadata.height=height;frame.metadata.near_plane=.05;frame.metadata.far_plane=768;
    frame.header.color_bytes=frame.header.depth_bytes=uint32_t(width*height*4);frame.pixels.resize(size_t(width)*height*8);
    float z=sky?1.f:float((768.-.05*768./5.)/(768.-.05));uint32_t raw=std::bit_cast<uint32_t>(z);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        size_t index=(size_t(y)*width+x)*4;frame.pixels[index]=y<height/2?255:0;frame.pixels[index+1]=255;frame.pixels[index+3]=255;
        for(size_t b=0;b<4;++b)frame.pixels[frame.header.color_bytes+index+b]=uint8_t(raw>>(8*b));
    }
    return frame;
}
bool pixel(std::span<const uint8_t> bytes,int width,int x,int y,std::array<uint8_t,3> expected){
    size_t i=(size_t(y)*width+x)*4;return std::equal(expected.begin(),expected.end(),bytes.begin()+std::ptrdiff_t(i));
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv){
    using namespace cc;
    try{
        bool live=false;double seconds=10;unsigned fps=20;std::filesystem::path output;uint16_t port=37122;
        for(int i=1;i<argc;++i){std::string arg=argv[i];
            if(arg=="--live")live=true;
            else if(arg=="--seconds"&&i+1<argc)seconds=std::stod(argv[++i]);
            else if(arg=="--fps"&&i+1<argc)fps=unsigned(std::stoul(argv[++i]));
            else if(arg=="--output"&&i+1<argc)output=argv[++i];
            else if(arg=="--port"&&i+1<argc){unsigned n=unsigned(std::stoul(argv[++i]));if(n<1||n>65535)throw std::runtime_error("Port range");port=uint16_t(n);}
            else throw std::runtime_error("Usage: countercraft_bench [--live --seconds 10 --fps 20 --port 37122] [--output file.bmp]");
        }
        require(std::isfinite(seconds)&&seconds>0&&seconds<=120&&fps>=1&&fps<=30,"Invalid duration/FPS");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        bool hardware=SUCCEEDED(hr);
        if(!hardware)check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Create WARP device");
        Compositor compositor(device.Get(),context.Get());CubeScene scene(device.Get(),context.Get());Texture composed;
        if(!live){
            for(auto size:{std::pair{80,60},std::pair{128,96}}){
                auto [w,h]=size;composed=texture(device.Get(),w,h,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);
                auto guest=synthetic(w,h);compositor.upload(guest);
                scene.draw(w,h,1.4f,1.4f,3);compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),w,h,.15f,10000,true);
                auto image=read_rgba(device.Get(),context.Get(),composed.texture.Get());
                require(pixel(image,w,w/2,h/2,{255,0,0}),"Near host cube must occlude guest plane");
                require(pixel(image,w,2,2,{0,255,0}),"GL top row flip");require(pixel(image,w,2,h-3,{255,255,0}),"GL bottom row flip");
                scene.draw(w,h,1.4f,1.4f,7);compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),w,h,.15f,10000,true);
                image=read_rgba(device.Get(),context.Get(),composed.texture.Get());
                require(!pixel(image,w,w/2,h/2,{255,0,0}),"Guest plane must occlude far host cube");
                compositor.upload(synthetic(w,h,true));compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),w,h,.15f,10000,true);
                image=read_rgba(device.Get(),context.Get(),composed.texture.Get());require(pixel(image,w,w/2,h/2,{255,0,0}),"Guest sky must not cover cube");
                compositor.clear();compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),w,h,.15f,10000,true);
                require(read_rgba(device.Get(),context.Get(),composed.texture.Get())==read_rgba(device.Get(),context.Get(),scene.color.texture.Get()),"No-frame pass-through");
            }
            if(!output.empty()){auto guest=synthetic(640,360);compositor.upload(guest);scene.draw(640,360,1.4f,1.4f,3);composed=texture(device.Get(),640,360,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),640,360,.15f,10000,true);save_bmp(output,640,360,read_rgba(device.Get(),context.Get(),composed.texture.Get()));}
            std::cout<<json{{"gpuDepthTests","passed"},{"hardware",hardware},{"featureLevel",unsigned(level)},{"resolutions",{{80,60},{128,96}}},{"groups",5}}.dump()<<"\n";
        }else{
            Receiver receiver(port,fps);int64_t began=monotonic_ns();uint64_t sequence=0,uploaded=0;std::vector<double> ages,uploads;
            int width=0,height=0;std::shared_ptr<const Frame> retained;
            while(double(monotonic_ns()-began)/1e9<seconds){
                auto status=receiver.stats();if(!status.failure.empty())throw std::runtime_error(status.failure);
                auto frame=receiver.latest();
                if(frame&&frame->header.sequence!=sequence){
                    int64_t upload_start=monotonic_ns();compositor.upload(*frame);uploads.push_back(double(monotonic_ns()-upload_start)/1e6);
                    if(width!=frame->metadata.width||height!=frame->metadata.height){width=frame->metadata.width;height=frame->metadata.height;composed=texture(device.Get(),width,height,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);}
                    scene.draw(width,height,float(frame->metadata.projection[0]),float(frame->metadata.projection[5]),2,.6f);
                    compositor.draw(scene.color.srv.Get(),scene.depth.srv.Get(),composed.rtv.Get(),width,height,.15f,10000,true);
                    ages.push_back(receiver.age_ms(*frame));sequence=frame->header.sequence;++uploaded;retained=std::move(frame);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            auto stats=receiver.stats();receiver.stop();require(uploaded>0,"No real Minecraft frames uploaded");
            if(!output.empty())save_bmp(output,width,height,read_rgba(device.Get(),context.Get(),composed.texture.Get()));
            std::sort(ages.begin(),ages.end());std::sort(uploads.begin(),uploads.end());
            size_t p95=size_t(double(ages.size()-1)*.95);
            std::cout<<json{{"nativeLive","passed"},{"hardware",hardware},{"received",stats.received},{"uploaded",uploaded},
                {"replaced",stats.replaced},{"stale",stats.stale},{"size",{width,height}},{"ageP95Ms",ages[p95]},
                {"uploadP95Ms",uploads[p95]},{"clockUncertaintyMs",double(stats.clock_uncertainty_ns)/1e6},
                {"session",retained->header.session.str()},{"requestedFrame",retained->metadata.requested_frame},{"cs2Integrated",false}}.dump()<<"\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
