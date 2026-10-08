#include "host_camera.hpp"
#include "projection.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace cc {
namespace {
using json=nlohmann::json;
uint32_t bounded(const json& value,uint32_t limit) {
    if(!value.is_number_integer() || value.is_boolean() || value.get<double>()<0 || value.get<double>()>limit)
        throw std::runtime_error("Invalid camera layout integer");
    return value.get<uint32_t>();
}
CameraLocation location(const json& data) {
    if(!data.is_object() || data.at("stage")!="VS") throw std::runtime_error("Only VS camera bindings supported");
    CameraLocation result{bounded(data.at("slot"),13),bounded(data.at("byteOffset"),65536-64)};
    if(result.offset%16) throw std::runtime_error("Camera matrix must be 16-byte aligned");
    const auto name=data.at("layout").get<std::string>();
    if(name!="row-major" && name!="column-major") throw std::runtime_error("Unknown camera matrix layout");
    result.column_major=name=="column-major"; return result;
}
CameraMatrix read(CameraLocation location,std::span<const CameraBuffer> buffers) {
    const CameraBuffer* chosen=nullptr;
    for(const auto& buffer:buffers) if(buffer.slot==location.slot) {
        if(chosen) throw std::runtime_error("Duplicate camera buffer slot");
        chosen=&buffer;
    }
    if(!chosen) throw std::runtime_error("Camera binding unavailable");
    const auto& b=*chosen;
    if(b.bytes.size()>65536 || b.bytes.size()%16 || b.first_constant>4096 || b.num_constants>4096
        || location.offset<b.first_constant*16 || uint64_t(location.offset)+64>b.bytes.size()
        || uint64_t(location.offset)+64>uint64_t(b.first_constant+b.num_constants)*16)
        throw std::runtime_error("Camera matrix is outside the bound buffer range");
    CameraMatrix m{};
    for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) {
        float value;
        const size_t index=location.column_major?c*4+r:r*4+c;
        std::memcpy(&value,b.bytes.data()+location.offset+index*4,4);
        if(!std::isfinite(value) || std::abs(value)>1e7f) throw std::runtime_error("Invalid camera matrix element");
        m[r*4+c]=value;
    }
    return m;
}
CameraMatrix multiply(const CameraMatrix& a,const CameraMatrix& b) {
    CameraMatrix out{};
    for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) for(size_t k=0;k<4;++k) out[r*4+c]+=a[r*4+k]*b[k*4+c];
    return out;
}
bool matches(const CameraMatrix& a,const CameraMatrix& b) {
    for(size_t i=0;i<16;++i) if(std::abs(a[i]-b[i])>1e-4+1e-6*std::abs(b[i])) return false;
    return true;
}
double dot(const std::array<double,3>& a,const std::array<double,3>& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
std::array<double,3> cross(const std::array<double,3>& a,const std::array<double,3>& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
}
CameraLayout CameraLayout::parse(const json& data) {
    if(data.at("schema")!=1 || data.at("kind")!="public-binding-camera-layout")
        throw std::runtime_error("Unsupported camera calibration schema");
    return {location(data.at("view")),location(data.at("projection")),location(data.at("worldVP")),location(data.at("relativeVP"))};
}
HostCamera decode_host_camera(const CameraLayout& layout,std::span<const CameraBuffer> buffers,const std::array<double,6>& viewport) {
    if(buffers.size()>14) throw std::runtime_error("Too many VS camera bindings");
    for(auto n:viewport) if(!std::isfinite(n)) throw std::runtime_error("Nonfinite camera viewport");
    if(viewport[2]<=0 || viewport[3]<=0 || viewport[4]<0 || viewport[5]>1 || viewport[4]>=viewport[5])
        throw std::runtime_error("Invalid camera viewport");
    HostCamera camera; camera.viewport=viewport;
    camera.view=read(layout.view,buffers); camera.projection=read(layout.projection,buffers);
    const auto& v=camera.view; const auto& p=camera.projection;
    if(std::abs(v[12])>1e-7 || std::abs(v[13])>1e-7 || std::abs(v[14])>1e-7 || std::abs(v[15]-1)>1e-7)
        throw std::runtime_error("Camera view must be affine");
    std::array<std::array<double,3>,3> rows{};
    for(size_t r=0;r<3;++r) for(size_t c=0;c<3;++c) rows[r][c]=v[r*4+c];
    for(size_t r=0;r<3;++r) for(size_t c=0;c<3;++c)
        if(std::abs(dot(rows[r],rows[c])-(r==c?1:0))>1e-5) throw std::runtime_error("Camera rotation is not orthonormal");
    if(std::abs(dot(rows[0],cross(rows[1],rows[2]))-1)>1e-5) throw std::runtime_error("Camera rotation is reflected");
    CameraMatrix column{}; for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) column[c*4+r]=p[r*4+c];
    const auto lens=ProjectionDepth::from_d3d_column_major(column).with_viewport(viewport[4],viewport[5]);
    if(std::abs(lens.aspect()/(viewport[2]/viewport[3])-1)>.005) throw std::runtime_error("Camera aspect disagrees with viewport");
    auto relative=v; relative[3]=relative[7]=relative[11]=0;
    const auto full=multiply(p,v), rotation=multiply(p,relative);
    if(matches(full,rotation) || !matches(read(layout.world_vp,buffers),full)
        || !matches(read(layout.relative_vp,buffers),rotation)) throw std::runtime_error("Camera matrix products disagree");
    for(size_t c=0;c<3;++c) {
        for(size_t r=0;r<3;++r) camera.position[c]-=v[r*4+c]*v[r*4+3];
        camera.forward[c]=(lens.right_handed()?-1:1)*rows[2][c]; camera.up[c]=rows[1][c];
    }
    constexpr double degrees=180/std::numbers::pi;
    camera.yaw=std::atan2(camera.forward[1],camera.forward[0])*degrees;
    camera.pitch=std::atan2(-camera.forward[2],std::hypot(camera.forward[0],camera.forward[1]))*degrees;
    const double yaw=camera.yaw/degrees;
    const std::array<double,3> nominal_right{std::sin(yaw),-std::cos(yaw),0};
    const auto nominal_up=cross(nominal_right,camera.forward);
    camera.roll=std::atan2(dot(camera.up,nominal_right),dot(camera.up,nominal_up))*degrees;
    camera.fov=lens.vertical_fov(); camera.aspect=lens.aspect(); camera.near_plane=lens.near_plane();camera.far_plane=lens.far_plane();
    camera.right_handed=lens.right_handed();camera.reversed=lens.reversed(); return camera;
}
nlohmann::json HostCamera::report() const {
    return {{"position",position},{"forward",forward},{"up",up},{"sourceYaw",yaw},{"sourcePitch",pitch},{"sourceRoll",roll},
        {"viewRowMajor",view},{"projectionRowMajor",projection},{"viewport",viewport},
        {"lens",{{"verticalFov",fov},{"aspect",aspect},{"near",near_plane},
        {"far",std::isfinite(far_plane)?json(far_plane):json(nullptr)},{"rightHanded",right_handed},{"reversedZ",reversed}}},
        {"mathematicalConsistency",true},{"sceneVerified",false},{"autoSelected",false}};
}
}
