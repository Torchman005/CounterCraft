#include "host_camera.hpp"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>

using namespace cc;
void require(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
void rejects(const std::function<void()>& call) {
    try {call();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid camera accepted");
}
int main() {
    try {
        const CameraMatrix view{1,0,0,-12, 0,0,1,-56, 0,-1,0,34, 0,0,0,1};
        const CameraMatrix p{.625,0,0,0, 0,1,0,0, 0,0,-100./98,-200./98, 0,0,-1,0};
        const CameraMatrix vp{.625,0,0,-7.5, 0,0,1,-56, 0,100./98,0,-3600./98, 0,1,0,-34};
        const CameraMatrix rp{.625,0,0,0, 0,0,1,0, 0,100./98,0,-200./98, 0,1,0,0};
        for(bool column:{false,true}) {
            std::vector<uint8_t> bytes(16+256);
            size_t base=16;
            for(const auto& m:{view,p,vp,rp}) {
                for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) {
                    const float f=float(m[r*4+c]);
                    std::memcpy(bytes.data()+base+4*(column?c*4+r:r*4+c),&f,4);
                }
                base+=64;
            }
            CameraLayout layout{{2,16,column},{2,80,column},{2,144,column},{2,208,column}};
            const CameraBuffer buffer{2,1,16,bytes};
            const std::array<double,6> viewport{0,0,160,100,0,.95};
            const auto camera=decode_host_camera(layout,std::span(&buffer,1),viewport);
            require(std::abs(camera.position[0]-12)<1e-4 && std::abs(camera.position[1]-34)<1e-4
                && std::abs(camera.position[2]-56)<1e-4,"Recover camera world origin");
            require(std::abs(camera.yaw-90)<1e-4 && std::abs(camera.pitch)<1e-4
                && std::abs(camera.roll)<1e-4,"Source orientation");
            require(std::abs(camera.fov-90)<1e-4 && std::abs(camera.near_plane-2)<1e-4
                && std::abs(camera.far_plane-100)<.001,"Lens and planes");
            auto short_range=buffer;short_range.num_constants=12;
            rejects([&]{decode_host_camera(layout,std::span(&short_range,1),viewport);});
            auto bad_viewport=viewport;bad_viewport[2]=200;
            rejects([&]{decode_host_camera(layout,std::span(&buffer,1),bad_viewport);});
            const auto original=bytes;
            const float wrong=123;
            std::memcpy(bytes.data()+144,&wrong,4);
            rejects([&]{decode_host_camera(layout,std::span(&buffer,1),viewport);});
            bytes=original;
            std::memcpy(bytes.data()+208,&wrong,4);
            rejects([&]{decode_host_camera(layout,std::span(&buffer,1),viewport);});
        }
        auto loc=nlohmann::json{{"stage","VS"},{"slot",2},{"byteOffset",16},{"layout","column-major"}};
        auto j=nlohmann::json{{"schema",1},{"kind","public-binding-camera-layout"},{"view",loc},{"projection",loc},{"worldVP",loc},{"relativeVP",loc}};
        require(CameraLayout::parse(j).view.slot==2,"Read caller calibration");
        for(auto bad:{nlohmann::json(-1),nlohmann::json(14),nlohmann::json(true),nlohmann::json(1.5)}) {
            auto invalid=j;invalid["view"]["slot"]=bad;rejects([&]{CameraLayout::parse(invalid);});
        }
        j["view"]["byteOffset"]=17;rejects([&]{CameraLayout::parse(j);});
        std::cout<<"Host camera bounded calibration, layout, pose and independent-product tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
