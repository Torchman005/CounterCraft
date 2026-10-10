#pragma once
#include "frame.hpp"
#include <optional>
#include <cmath>
#include <stdexcept>
#include <json.hpp>
#include <vector>

namespace cc {
struct FusionPolicy {
    std::array<float,2> world{},foreground{};
    float clear{};
    uint32_t camera_draw{64};
    std::vector<std::array<float,2>> additional_boundaries;
    uint32_t diagnostic_view{};
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
        if(j.contains("additionalBoundaryDepthRanges")){
            p.additional_boundaries=j.at("additionalBoundaryDepthRanges").get<std::vector<std::array<float,2>>>();
            if(p.additional_boundaries.size()>4)throw std::runtime_error("Too many fusion boundaries");
            for(const auto& range:p.additional_boundaries)
                if(!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0]<0 || range[1]>1 || range[0]>=range[1] || range==p.world)
                    throw std::runtime_error("Invalid additional fusion boundary");
        }
        if(j.contains("diagnosticView")){
            const auto& value=j.at("diagnosticView");
            if(!value.is_number_unsigned() && !value.is_number_integer())throw std::runtime_error("Invalid fusion diagnostic view");
            if(value.get<double>()<0 || value.get<double>()>3)throw std::runtime_error("Invalid fusion diagnostic view");
            p.diagnostic_view=value.get<uint32_t>();
        }
        return p;
    }
};

// A camera copied in the world pass can only accompany the first following
// world -> foreground boundary on that same resource and effect interval.
// No draw index identifies the boundary. Once latched, coverage tracks later
// writes independently of their viewport range; the snapshot never changes.
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
        if(latched)return false; // adapter must accumulate coverage for every later resource
        if(r!=resource){invalid=true;rejection=1;return false;}
        if(v==viewport)return false;
        auto expected=viewport;expected[4]=p.foreground[0];expected[5]=p.foreground[1];
        bool calibrated=v==expected;
        for(const auto& range:p.additional_boundaries){expected[4]=range[0];expected[5]=range[1];calibrated=calibrated || v==expected;}
        if(calibrated){latched=true;return true;}
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
