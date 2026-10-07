#include "compositor.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace cc;
void require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
Frame guest(int w,int h,bool sky=false) {
    Frame frame; frame.metadata.width=w;frame.metadata.height=h;
    frame.metadata.near_plane=.05;frame.metadata.far_plane=768;
    frame.header.color_bytes=frame.header.depth_bytes=uint32_t(w*h*4);frame.pixels.resize(size_t(w)*h*8);
    const float depth=sky ? 1.f : float((768.-.05*768./5)/(768.-.05));
    const auto raw=std::bit_cast<uint32_t>(depth);
    for(size_t i=0;i<size_t(w)*h;++i) {
        frame.pixels[i*4+1]=frame.pixels[i*4+3]=255;
        for(size_t b=0;b<4;++b) frame.pixels[frame.header.color_bytes+i*4+b]=uint8_t(raw>>(8*b));
    }
    return frame;
}
}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
        const bool hardware=SUCCEEDED(hr);
        if(!hardware) check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Create projection test device");
        Compositor compositor(device.Get(),context.Get());
        unsigned cases=0;
        for(auto size:{std::pair{32,24},std::pair{64,48}}) {
            const auto [w,h]=size;
            std::vector<uint8_t> host_rgba(size_t(w)*h*4);
            for(size_t i=0;i<size_t(w)*h;++i) host_rgba[i*4]=host_rgba[i*4+3]=255;
            auto host_color=texture(device.Get(),w,h,DXGI_FORMAT_R8G8B8A8_UNORM,host_rgba.data());
            auto output=texture(device.Get(),w,h,DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,true);
            for(bool rh:{false,true}) for(bool reverse:{false,true}) for(bool infinite:{false,true}) for(float units:{.5f,1.f,32.f}) {
                // Independently rasterized depth field: host planes at distance 2/9, guest at 5.
                // Forward projection is evaluated on CPU; the shader solves its inverse.
                const double sign=rh ? -1 : 1;
                const double a=(reverse ? (infinite ? 0 : -.01) : (infinite ? 1 : 1.01))*sign;
                const double b=(reverse ? 1 : -1)*(infinite ? 1 : 1.01)*units;
                std::array<double,16> matrix={1.125,0,0,0, 0,2,0,0, .001,-.002,a,sign, 0,0,b,0};
                auto projection=ProjectionDepth::from_d3d_column_major(matrix);
                std::vector<float> host_depth(size_t(w)*h);
                for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                    const double eye_z=sign*(x<w/2 ? 2 : 9)*units;
                    host_depth[size_t(y)*w+x]=float((a*eye_z+b)/(sign*eye_z));
                }
                host_depth[0]=reverse ? 0.f : 1.f; // Far/sky endpoint must admit nearer guest.
                host_depth[1]=std::numeric_limits<float>::quiet_NaN();
                host_depth[2]=-.1f;host_depth[3]=1.1f; // Invalid host depth preserves host colour.
                auto depth_texture=texture(device.Get(),w,h,DXGI_FORMAT_R32_FLOAT,host_depth.data());
                compositor.upload(guest(w,h));
                compositor.draw(host_color.srv.Get(),depth_texture.srv.Get(),output.rtv.Get(),w,h,projection,true,units);
                const auto pixels=read_rgba(device.Get(),context.Get(),output.texture.Get());
                for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                    const bool host=(x<w/2 && (y!=0 || x!=0)) || (y==0 && x>=1 && x<=3);
                    const size_t i=(size_t(y)*w+x)*4;
                    require(pixels[i]==(host ? 255 : 0) && pixels[i+1]==(host ? 0 : 255)
                        && pixels[i+2]==0 && pixels[i+3]==255,"Projection GPU occlusion/invalid-depth mismatch");
                }
                compositor.upload(guest(w,h,true));
                compositor.draw(host_color.srv.Get(),depth_texture.srv.Get(),output.rtv.Get(),w,h,projection,true,units);
                require(read_rgba(device.Get(),context.Get(),output.texture.Get())==host_rgba,"Guest sky must preserve host exactly");
                compositor.clear();
                compositor.draw(host_color.srv.Get(),depth_texture.srv.Get(),output.rtv.Get(),w,h,projection,true,units);
                require(read_rgba(device.Get(),context.Get(),output.texture.Get())==host_rgba,"No frame must preserve host exactly");
                if(!rh && !reverse && !infinite && units==1) {
                    for(float invalid:{0.f,1e-10f,1e10f,std::numeric_limits<float>::quiet_NaN()}) {
                        bool refused=false;
                        try {compositor.draw(host_color.srv.Get(),depth_texture.srv.Get(),output.rtv.Get(),w,h,projection,true,invalid);}
                        catch(const std::exception&) {refused=true;}
                        require(refused,"Invalid unit scale accepted");
                    }
                }
                ++cases;
            }
        }
        context->ClearState();
        std::cout<<"{\"projectionGpuTests\":\"passed\",\"hardware\":"<<(hardware ? "true" : "false")
            <<",\"modes\":8,\"unitScales\":[0.5,1,32],\"cases\":"<<cases<<",\"cs2Integrated\":false}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
