#include "host_probe.hpp"
#include "depth_view.hpp"
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
    if(depth_description(desc,description)) probe->observe({.kind=DepthEventKind::observe,.device=key(device),
        .resource=resource.handle,.description=description,.new_lifetime=true});
}
void destroy_resource(api::device* device,api::resource resource) {
    if(probe && d3d11(device)) probe->observe({.kind=DepthEventKind::destroy_resource,.device=key(device),.resource=resource.handle});
}
void destroy_device(api::device* device) {
    if(probe) probe->observe({.kind=DepthEventKind::destroy_device,.device=key(device)});
}
void reset_command(api::command_list* cmd) {
    if(probe && cmd) probe->observe({.kind=DepthEventKind::unbind,.device=key(cmd->get_device()),.command=key(cmd)});
}
void bind_depth(api::command_list* cmd,uint32_t,const api::resource_view*,api::resource_view dsv) {
    if(!immediate(cmd)) return;
    try {
        auto* device=cmd->get_device();
        if(!dsv.handle) { probe->observe({.kind=DepthEventKind::unbind,.device=key(device),.command=key(cmd)}); return; }
        const auto resource=device->get_resource_from_view(dsv);
        DepthDescription description;
        if(!resource.handle || !depth_description(device->get_resource_desc(resource),description)) {
            probe->observe({.kind=DepthEventKind::unbind,.device=key(device),.command=key(cmd)}); return;
        }
        const auto view=describe_depth_view(device->get_resource_view_desc(dsv),description);
        probe->observe({.kind=DepthEventKind::bind,.device=key(device),.command=key(cmd),
            .resource=resource.handle,.description=description,.view=view});
    } catch (...) { probe->failed(); }
}
bool draw(api::command_list* cmd,uint32_t count,uint32_t instances,uint32_t,uint32_t) {
    if(immediate(cmd)) probe->observe({.kind=DepthEventKind::draw,.device=key(cmd->get_device()),
        .command=key(cmd),.elements=count,.instances=instances});
    return false; // Never suppress or replace a game command.
}
bool draw_indexed(api::command_list* cmd,uint32_t count,uint32_t instances,uint32_t,int32_t,uint32_t) {
    return draw(cmd,count,instances,0,0);
}
bool indirect(api::command_list* cmd,api::indirect_command type,api::resource,uint64_t,uint32_t count,uint32_t) {
    if((type==api::indirect_command::draw || type==api::indirect_command::draw_indexed) && immediate(cmd))
        probe->observe({.kind=DepthEventKind::draw,.device=key(cmd->get_device()),
            .command=key(cmd),.elements=count,.instances=1,.indirect=true});
    return false;
}
bool clear(api::command_list* cmd,api::resource_view dsv,const float* depth,const uint8_t*,uint32_t,const api::rect*) {
    if(!depth || !dsv.handle || !immediate(cmd)) return false;
    try {
        auto* device=cmd->get_device();
        const auto resource=device->get_resource_from_view(dsv);
        DepthDescription description;
        if(resource.handle && depth_description(device->get_resource_desc(resource),description)) {
            const auto view=describe_depth_view(device->get_resource_view_desc(dsv),description);
            probe->observe({.kind=DepthEventKind::clear,.device=key(device),.resource=resource.handle,
                .description=description,.view=view,.clear_value=*depth,.has_view=true});
        }
    } catch (...) { probe->failed(); }
    return false;
}
void begin_effects(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(!probe || !d3d11(runtime->get_device())) return;
    uint32_t w=0,h=0; runtime->get_screenshot_width_and_height(&w,&h);
    probe->observe({.kind=DepthEventKind::begin_effects,.device=key(runtime->get_device()),.width=w,.height=h});
}
void end_effects(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(probe && d3d11(runtime->get_device())) probe->observe({.kind=DepthEventKind::end_effects,.device=key(runtime->get_device())});
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
const char* view_type_name(DepthViewType type) {
    switch(type) {
    case DepthViewType::texture_2d:return "texture_2d";
    case DepthViewType::texture_2d_array:return "texture_2d_array";
    case DepthViewType::texture_2d_multisample:return "texture_2d_multisample";
    case DepthViewType::texture_2d_multisample_array:return "texture_2d_multisample_array";
    default:return "unsupported";
    }
}
nlohmann::json subresources(const DepthSubresources& range) {
    return {{"firstLevel",range.first_level},{"levels",range.levels},{"firstLayer",range.first_layer},{"layers",range.layers}};
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
    const auto snapshot=observer_.snapshot();
    nlohmann::json devices=nlohmann::json::array();
    for(const auto& report:snapshot.devices) {
        if(!report.device_id) continue;
        nlohmann::json candidates=nlohmann::json::array();
        for(uint32_t i=0;i<report.count;++i) {
            const auto& c=report.candidates[i]; const auto& d=c.description; const auto& stats=c.counts;
            nlohmann::json views=nlohmann::json::array();
            for(uint32_t j=0;j<stats.view_count;++j) {
                const auto& v=stats.views[j]; const auto& view=v.description;
                views.push_back({{"type",view_type_name(view.type)},{"typeValue",uint32_t(view.type)},{"format",view.format},
                    {"raw",subresources(view.raw)},{"normalized",subresources(view.normalized)},
                    {"formatCompatible",view.format_compatible},{"canonicalBaseDsv",view.canonical},
                    {"binds",v.binds},{"draws",v.draws},{"directElements",v.elements},{"indirectDraws",v.indirect},{"clears",v.clears},
                    {"lastClear",v.has_clear ? nlohmann::json(v.last_clear) : nlohmann::json(nullptr)}});
            }
            candidates.push_back({{"id",c.id},{"size",{d.width,d.height}},{"kind",kind_name(d.kind)},
                {"format",d.format},{"samples",d.samples},{"layers",d.layers},{"levels",d.levels},
                {"shaderReadable",d.shader_readable},{"samplingShapeOnly",d.sampling_shape()},
                {"matchesOutput",c.matches_output},{"draws",stats.draws},{"directElements",stats.elements},
                {"indirectDraws",stats.indirect},{"nonBaseViewDraws",stats.non_base_view_draws},{"clears",stats.clears},
                {"lastClear",stats.has_clear ? nlohmann::json(stats.last_clear) : nlohmann::json(nullptr)},
                {"views",views}});
        }
        devices.push_back({{"deviceId",report.device_id},{"interval",report.interval},{"outputSize",{report.width,report.height}},
            {"candidates",candidates}});
    }
    nlohmann::json rejected=nlohmann::json::object();
    constexpr std::array names={"observeResource","destroyResource","destroyDevice","bindDepth","unbindDepth",
        "draw","clearDepth","beginEffects","endEffects"};
    static_assert(names.size()==size_t(DepthEventKind::count));
    for(size_t i=0;i<names.size();++i) rejected[names[i]]=snapshot.rejected_by_kind[i];
    return {{"enabled",true},{"readOnly",true},{"cameraDepthVerified",false},{"autoSelected",false},
        {"missedEvents",snapshot.missed()},{"deferredEvents",snapshot.deferred_events},{"overflow",snapshot.overflow},
        {"knownLossFree",snapshot.known_loss_free()},{"devices",devices},
        {"queueCapacity",DepthObserver::queue_capacity},{"eventBytes",sizeof(DepthEvent)},
        {"eventsQueued",snapshot.queued},{"eventsProcessed",snapshot.processed},{"pendingEvents",snapshot.pending},
        {"peakPendingAtDrain",snapshot.peak_pending_at_drain},{"queueFullEvents",snapshot.full_events},
        {"queueContentionEvents",snapshot.contended_events},{"callbackFailures",snapshot.callback_failures},
        {"rejectedEventsByKind",rejected},{"drainBatches",snapshot.drain_batches},
        {"maxBatchEvents",snapshot.max_batch_events},{"totalDrainUs",snapshot.total_drain_us},
        {"maxDrainUs",snapshot.max_drain_us},{"snapshotBuildUs",snapshot.snapshot_build_us}};
}
}
