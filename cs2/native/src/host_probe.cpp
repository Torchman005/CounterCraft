#include "host_probe.hpp"
#include <reshade.hpp>
#include <d3d11.h>

namespace cc {
namespace {
namespace api=reshade::api;
HostProbe* probe=nullptr; // Explicit registration lifetime, no owning destructor.
uint64_t key(api::device* device) { return reinterpret_cast<uintptr_t>(device); }
uint64_t key(api::command_list* cmd) { return reinterpret_cast<uintptr_t>(cmd); }
bool d3d11(api::device* device) { return device && device->get_api()==api::device_api::d3d11; }
// Used only in recording callbacks: get_native then denotes ID3D11DeviceContext.
// Never retain it, call QueryInterface/AddRef, or inspect native command lists.
bool immediate(api::command_list* cmd) {
    if(!probe || !cmd || !d3d11(cmd->get_device())) return false;
    auto* native=reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
    if(!native) { probe->failed(); return false; }
    if(native->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) { probe->deferred(); return false; }
    return true;
}
bool depth_description(const api::resource_desc& desc,DepthDescription& output) {
    if(desc.type!=api::resource_type::texture_2d || (desc.usage & api::resource_usage::depth_stencil)==api::resource_usage::undefined)
        return false;
    DepthKind kind=DepthKind::unknown;
    switch(desc.texture.format) {
    case api::format::d16_unorm: case api::format::r16_typeless: kind=DepthKind::d16; break;
    case api::format::d24_unorm_s8_uint: case api::format::r24_g8_typeless: kind=DepthKind::d24s8; break;
    case api::format::d32_float: case api::format::r32_typeless: kind=DepthKind::d32; break;
    case api::format::d32_float_s8_uint: case api::format::r32_g8_typeless: kind=DepthKind::d32s8; break;
    default: break;
    }
    output={desc.texture.width,desc.texture.height,desc.texture.depth_or_layers,desc.texture.levels,
        desc.texture.samples,uint32_t(desc.texture.format),kind,
        (desc.usage & api::resource_usage::shader_resource)!=api::resource_usage::undefined};
    return true;
}
void init_resource(api::device* device,const api::resource_desc& desc,const api::subresource_data*,api::resource_usage,api::resource resource) {
    if(!probe || !d3d11(device)) return;
    DepthDescription description;
    if(depth_description(desc,description)) probe->observe([&](auto& p){p.observe({key(device),resource.handle},description,true);});
}
void destroy_resource(api::device* device,api::resource resource) {
    if(probe && d3d11(device)) probe->observe([&](auto& p){p.destroy_resource({key(device),resource.handle});});
}
void destroy_device(api::device* device) {
    if(probe) probe->observe([&](auto& p){p.destroy_device(key(device));});
}
void reset_command(api::command_list* cmd) {
    if(probe && cmd) probe->observe([&](auto& p){p.unbind(key(cmd->get_device()),key(cmd));});
}
void bind_depth(api::command_list* cmd,uint32_t,const api::resource_view*,api::resource_view dsv) {
    if(!immediate(cmd)) return;
    try {
        auto* device=cmd->get_device();
        if(!dsv.handle) { probe->observe([&](auto& p){p.unbind(key(device),key(cmd));}); return; }
        const auto resource=device->get_resource_from_view(dsv);
        DepthDescription description;
        if(!resource.handle || !depth_description(device->get_resource_desc(resource),description)) {
            probe->observe([&](auto& p){p.unbind(key(device),key(cmd));}); return;
        }
        const auto view=device->get_resource_view_desc(dsv);
        const bool base=view.type==api::resource_view_type::texture_2d && view.texture.first_level==0
            && view.texture.levels==1 && view.texture.first_layer==0 && view.texture.layers==1;
        probe->observe([&](auto& p){p.bind(key(device),key(cmd),resource.handle,description,base);});
    } catch (...) { probe->failed(); }
}
bool draw(api::command_list* cmd,uint32_t count,uint32_t instances,uint32_t,uint32_t) {
    if(immediate(cmd)) probe->observe([&](auto& p){p.draw(key(cmd->get_device()),key(cmd),count,instances);});
    return false; // Never suppress or replace a game command.
}
bool draw_indexed(api::command_list* cmd,uint32_t count,uint32_t instances,uint32_t,int32_t,uint32_t) {
    return draw(cmd,count,instances,0,0);
}
bool indirect(api::command_list* cmd,api::indirect_command type,api::resource,uint64_t,uint32_t count,uint32_t) {
    if((type==api::indirect_command::draw || type==api::indirect_command::draw_indexed) && immediate(cmd))
        probe->observe([&](auto& p){p.draw(key(cmd->get_device()),key(cmd),count,1,true);});
    return false;
}
bool clear(api::command_list* cmd,api::resource_view dsv,const float* depth,const uint8_t*,uint32_t,const api::rect*) {
    if(!depth || !dsv.handle || !immediate(cmd)) return false;
    try {
        auto* device=cmd->get_device();
        const auto resource=device->get_resource_from_view(dsv);
        DepthDescription description;
        if(resource.handle && depth_description(device->get_resource_desc(resource),description))
            probe->observe([&](auto& p){p.clear({key(device),resource.handle},description,*depth);});
    } catch (...) { probe->failed(); }
    return false;
}
void begin_effects(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(!probe || !d3d11(runtime->get_device())) return;
    uint32_t w=0,h=0; runtime->get_screenshot_width_and_height(&w,&h);
    probe->observe([&](auto& p){p.begin_effects(key(runtime->get_device()),w,h);});
}
void end_effects(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(probe && d3d11(runtime->get_device())) probe->observe([&](auto& p){p.end_effects(key(runtime->get_device()));});
}
void secondary(api::command_list* cmd,api::command_list*) {
    if(probe && cmd && d3d11(cmd->get_device())) {
        probe->deferred(); // Deferred playback/restore is not reconstructed in this observer.
        reset_command(cmd);
    }
}
const char* kind_name(DepthKind kind) {
    switch(kind) {
    case DepthKind::d16:return "d16";
    case DepthKind::d24s8:return "d24s8";
    case DepthKind::d32:return "d32";
    case DepthKind::d32s8:return "d32s8";
    default:return "unknown";
    }
}
}
HostProbe::~HostProbe() { uninstall(); }
void HostProbe::install() {
    if(!enabled_ || installed_) return;
    probe=this; installed_=true;
    reshade::register_event<reshade::addon_event::init_resource>(init_resource);
    reshade::register_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::register_event<reshade::addon_event::destroy_device>(destroy_device);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind_depth);
    reshade::register_event<reshade::addon_event::draw>(draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(draw_indexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(clear);
    reshade::register_event<reshade::addon_event::reset_command_list>(reset_command);
    reshade::register_event<reshade::addon_event::destroy_command_list>(reset_command);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(end_effects);
}
void HostProbe::uninstall() {
    if(!installed_) return;
    reshade::unregister_event<reshade::addon_event::init_resource>(init_resource);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(destroy_device);
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind_depth);
    reshade::unregister_event<reshade::addon_event::draw>(draw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(draw_indexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(clear);
    reshade::unregister_event<reshade::addon_event::reset_command_list>(reset_command);
    reshade::unregister_event<reshade::addon_event::destroy_command_list>(reset_command);
    reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
    reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(end_effects);
    probe=nullptr; installed_=false;
}
nlohmann::json HostProbe::report() const {
    if(!enabled_) return {{"enabled",false}};
    DepthInventory copy;
    { std::lock_guard lock(mutex_); copy=inventory_; }
    const auto missed=missed_events_.load(), deferred=deferred_events_.load();
    nlohmann::json devices=nlohmann::json::array();
    for(const auto& report:copy.reports()) {
        if(!report.device_id) continue;
        nlohmann::json candidates=nlohmann::json::array();
        for(uint32_t i=0;i<report.count;++i) {
            const auto& c=report.candidates[i]; const auto& d=c.description; const auto& stats=c.counts;
            candidates.push_back({{"id",c.id},{"size",{d.width,d.height}},{"kind",kind_name(d.kind)},
                {"format",d.format},{"samples",d.samples},{"layers",d.layers},{"levels",d.levels},
                {"shaderReadable",d.shader_readable},{"samplingShapeOnly",d.sampling_shape()},
                {"matchesOutput",c.matches_output},{"draws",stats.draws},{"directElements",stats.elements},
                {"indirectDraws",stats.indirect},{"nonBaseViewDraws",stats.non_base_view_draws},{"clears",stats.clears},
                {"lastClear",stats.has_clear ? nlohmann::json(stats.last_clear) : nlohmann::json(nullptr)}});
        }
        devices.push_back({{"deviceId",report.device_id},{"interval",report.interval},{"outputSize",{report.width,report.height}},
            {"candidates",candidates}});
    }
    return {{"enabled",true},{"readOnly",true},{"cameraDepthVerified",false},{"autoSelected",false},
        {"missedEvents",missed},{"deferredEvents",deferred},{"overflow",copy.overflow()},
        {"knownLossFree",missed==0 && deferred==0 && copy.overflow()==0},{"devices",devices}};
}
}
