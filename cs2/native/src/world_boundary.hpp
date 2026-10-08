#pragma once
#include "frame.hpp"
#include <optional>
#include <cmath>
#include <stdexcept>
#include <json.hpp>

namespace cc {
struct FusionPolicy {
    std::array<float,2> world{},foreground{};
    float clear{};
    uint32_t camera_draw{64};
    static FusionPolicy parse(const nlohmann::json& j) {
        if(j.at("schema")!=1 || j.at("kind")!="local-world-boundary")
            throw std::runtime_error("Unsupported fusion policy");
        FusionPolicy p{j.at("worldDepthRange").get<std::array<float,2>>(),
            j.at("foregroundDepthRange").get<std::array<float,2>>(),j.at("clearDepth").get<float>(),
            j.at("cameraDraw").get<uint32_t>()};
        for(const auto& range:{p.world,p.foreground})
            if(!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0]<0 || range[1]>1 || range[0]>=range[1])
                throw std::runtime_error("Invalid fusion viewport range");
        if(p.world==p.foreground || !std::isfinite(p.clear) || p.clear<0 || p.clear>1 || !p.camera_draw || p.camera_draw>4096)
            throw std::runtime_error("Invalid fusion boundary policy");
        return p;
    }
};

// A camera copied in the world pass can only accompany the first following
// world -> foreground boundary on that same resource and effect interval.
// No draw index identifies the boundary. A second candidate or re-entry rejects
// the whole interval instead of guessing which depth belongs to the scene.
struct WorldBoundary {
    uint64_t resource{},sequence{};
    bool armed{},latched{},invalid{};
    uint32_t rejection{};
    std::array<double,6> viewport{};
    void reset(){*this={};}
    void arm(uint64_t r,uint64_t s,const std::array<double,6>& v) {
        if(armed || invalid || !r || !s){invalid=true;return;}
        resource=r;sequence=s;viewport=v;armed=true;
    }
    void clear_resource(uint64_t r,bool known_late_clear=false){if(armed && resource==r){
        if(latched && known_late_clear)return;
        invalid=true;rejection=3;
    }}
    bool observe(uint64_t r,const std::array<double,6>& v,const FusionPolicy& p) {
        if(!armed || invalid)return false;
        if(r!=resource){invalid=true;rejection=1;return false;}
        // Later draws with the original viewport are conservatively protected
        // by final-depth comparison; they cannot overwrite the latched snapshot.
        if(v==viewport)return false;
        auto expected=viewport;expected[4]=p.foreground[0];expected[5]=p.foreground[1];
        if(!latched && v==expected){latched=true;return true;}
        if(latched && v==expected)return false;
        invalid=true;rejection=2;return false;
    }
    bool ready()const{return armed && latched && !invalid;}
};

// Static composition is safe only when the rendered guest was actually checked
// against its requested host pose, and that pose still describes today's rays.
// Motion/resize/lens changes show the host until a matching guest arrives.
inline bool fusion_aligned(const Frame& frame,const HostCamera& host) {
    if(frame.metadata.full_client || frame.metadata.gui_open || !frame.relayed_camera)return false;
    const auto& prior=*frame.relayed_camera;
    for(size_t i=0;i<3;++i)if(std::abs(prior.position[i]-host.position[i])>.005)return false;
    for(size_t i=0;i<16;++i)
        if(std::abs(prior.view[i]-host.view[i])>1e-5 || std::abs(prior.projection[i]-host.projection[i])>1e-5)return false;
    return prior.viewport==host.viewport && host.right_handed && std::abs(host.roll)<.01;
}
}
