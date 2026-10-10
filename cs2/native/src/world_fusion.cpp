#include "world_fusion.hpp"
#include "world_boundary.hpp"
#include "camera_readback.hpp"
#include "camera_gpu.hpp"
#include "depth_sampler.hpp"
#include "depth_coverage.hpp"
#include "coverage_journal.hpp"
#include "receiver.hpp"
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace cc {
namespace api=reshade::api;
using Microsoft::WRL::ComPtr;
struct WorldFusion::Impl {
    struct Candidate {uint64_t draws{};bool cleared{};uint32_t id{};};
    struct Transition {std::array<double,6> viewport{};uint32_t rejection{};bool latched{};
        uint32_t resource{},color{},samples{},event{},source{},destination{};bool backbuffer{},writes_depth{};};
    struct Device {
        uint32_t width{},height{};
        uint64_t bound{},current_sequence{};
        uint64_t color{},backbuffer{};
        uint32_t next_resource_id{},next_color_id{};
        std::unordered_map<uint64_t,uint32_t> colors;
        bool effects{},available{},unsupported_depth{};
        bool late_clear{},output_late_clear{};
        CoverageJournal journal;
        WorldBoundary boundary;
        std::unordered_map<uint64_t,Candidate> candidates;
        std::unordered_map<uint64_t,bool> pending;
        std::unique_ptr<CameraReadback> cameras;
        std::unique_ptr<CameraGpu> matrices;
        std::unique_ptr<DepthSampler> world,final;
        std::unique_ptr<DepthSampler> auxiliary;
        ComPtr<ID3D11ShaderResourceView> auxiliary_view;
        std::unique_ptr<DepthCoverage> coverage;
        ComPtr<ID3D11ShaderResourceView> world_view,final_view;
        std::optional<HostCamera> camera;
        std::array<double,6> output_viewport{};
        std::array<Transition,128> transitions{};
        size_t transition_count{};
        bool trace_truncated{};
        Transition last_draw{},last_bind{};
    };
    bool enabled{},installed{};
    CameraLayout layout;
    FusionPolicy policy;
    Receiver& receiver;
    mutable std::recursive_mutex mutex;
    std::unordered_map<api::device*,Device> devices;
    std::atomic<uint64_t> sequence{},snapshots{},boundaries{},paired{},gpu_pairs{},shown{},unaligned{},not_ready{},invalid{};
    std::string error;
    uint32_t last_rejection{};
    std::array<Transition,128> last_transitions{};
    size_t last_transition_count{};
    bool last_trace_truncated{};
    std::optional<HostCamera> last_camera;
    Impl(bool e,const std::filesystem::path& base,Receiver& r):enabled(e),receiver(r) {
        if(!enabled)return;
        auto read=[&](const char* name){auto path=base/name;
            if(std::filesystem::file_size(path)>65536)throw std::runtime_error("Fusion calibration too large");
            std::ifstream input(path);return nlohmann::json::parse(input);};
        layout=CameraLayout::parse(read("camera-layout.json"));
        policy=FusionPolicy::parse(read("fusion-policy.json"));
    }
};
namespace {
WorldFusion::Impl* fusion=nullptr;
ID3D11DeviceContext* immediate(api::command_list* cmd) {
    if(!fusion || !cmd || cmd->get_device()->get_api()!=api::device_api::d3d11)return nullptr;
    auto* c=reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
    return c && c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE?c:nullptr;
}
bool eligible(api::device* device,api::resource resource,const WorldFusion::Impl::Device& d) {
    if(!resource.handle || !d.width || !d.height)return false;
    const auto desc=device->get_resource_desc(resource);
    return desc.type==api::resource_type::texture_2d && desc.texture.width==d.width && desc.texture.height==d.height
        && uint64_t(d.width)*d.height<=max_pixels && desc.texture.depth_or_layers==1 && desc.texture.levels==1
        && desc.texture.samples<=8 && (desc.usage & api::resource_usage::depth_stencil)!=api::resource_usage::undefined;
}
void capture_coverage(WorldFusion::Impl::Device&,ID3D11DeviceContext*,ID3D11Texture2D*,float);
void capture_resource(WorldFusion::Impl::Device& d,ID3D11DeviceContext* context,uint64_t resource){
    if(!resource || !d.boundary.ready())return;
    d.journal.capture(resource,[&](uint64_t handle,bool){
        capture_coverage(d,context,reinterpret_cast<ID3D11Texture2D*>(handle),fusion->policy.clear);
    });
}
void reject_coverage(WorldFusion::Impl::Device& d,const char* reason){
    d.boundary.invalid=true;d.boundary.rejection=6;throw std::runtime_error(reason);
}
void trace(WorldFusion::Impl::Device& d,uint32_t event,const std::array<double,6>& viewport={},uint32_t source=0,uint32_t destination=0,bool writes=false){
    if(!d.boundary.armed)return;
    uint32_t id=0,samples=0;
    if(const auto found=d.candidates.find(d.bound);found!=d.candidates.end()){
        id=found->second.id;D3D11_TEXTURE2D_DESC desc{};
        reinterpret_cast<ID3D11Texture2D*>(d.bound)->GetDesc(&desc);samples=desc.SampleDesc.Count;
    }
    const auto color=d.colors.find(d.color);const uint32_t color_id=color==d.colors.end()?0:color->second;
    const WorldFusion::Impl::Transition t{viewport,d.boundary.rejection,d.boundary.latched,id,color_id,samples,event,source,destination,d.color && d.color==d.backbuffer,writes};
    if(event==1 || event==2){auto& prior=event==1?d.last_bind:d.last_draw;
        if(prior.event==event && prior.viewport==t.viewport && prior.resource==t.resource && prior.color==t.color
            && prior.rejection==t.rejection && prior.writes_depth==writes)return;
        prior=t;}
    if(d.transition_count==d.transitions.size()){d.trace_truncated=true;return;}
    d.transitions[d.transition_count++]=t;
}
void bind_depth(api::command_list* cmd,uint32_t count,const api::resource_view* colors,api::resource_view view) {
    if(!immediate(cmd))return;
    std::lock_guard lock(fusion->mutex);
    try {
        auto* device=cmd->get_device();auto& d=fusion->devices[device];
        const auto next=view.handle?device->get_resource_from_view(view):api::resource{};
        if(d.bound && next.handle!=d.bound)capture_resource(d,immediate(cmd),d.bound);
        d.bound=0;d.color=0;d.unsupported_depth=false;
        if(count && colors && colors[0].handle){d.color=device->get_resource_from_view(colors[0]).handle;
            if(d.colors.size()<128 && !d.colors.contains(d.color))d.colors[d.color]=++d.next_color_id;}
        if(d.effects)return;
        if(!view.handle){trace(d,1);return;}
        const auto resource=device->get_resource_from_view(view);
        if(!eligible(device,resource,d)){d.unsupported_depth=true;trace(d,1);return;}
        if(!d.candidates.contains(resource.handle) && d.candidates.size()>=16){d.boundary.invalid=true;throw std::runtime_error("Too many depth resources");}
        auto [it,inserted]=d.candidates.try_emplace(resource.handle);if(inserted)it->second.id=++d.next_resource_id;
        d.bound=resource.handle;trace(d,1);
    }catch(const std::exception& e){auto& d=fusion->devices[cmd->get_device()];d.boundary.invalid=true;++fusion->invalid;fusion->error=std::string(e.what()).substr(0,160);}
}
bool clear_depth(api::command_list* cmd,api::resource_view view,const float* depth,const uint8_t*,uint32_t count,const api::rect*) {
    if(!immediate(cmd) || !depth)return false;
    std::lock_guard lock(fusion->mutex);
    try {
        auto* device=cmd->get_device();auto& d=fusion->devices[device];
        if(d.effects)return false;
        const auto resource=device->get_resource_from_view(view);
        if(!eligible(device,resource,d)){
            if(d.boundary.ready())reject_coverage(d,"Unsupported post-world depth clear");
            return false;
        }
        if(!d.candidates.contains(resource.handle) && d.candidates.size()>=16){d.boundary.invalid=true;throw std::runtime_error("Too many depth resources");}
        auto [it,inserted]=d.candidates.try_emplace(resource.handle);if(inserted)it->second.id=++d.next_resource_id;
        const bool known_clear=count==0 && *depth==fusion->policy.clear;
        if(d.boundary.ready() && !known_clear){d.boundary.invalid=true;d.boundary.rejection=3;}
        trace(d,known_clear?3:4,{},d.candidates.contains(resource.handle)?d.candidates.at(resource.handle).id:0);
        if(d.boundary.ready() && known_clear){
            // The event precedes the clear: preserve weapon/later writes now.
            d.journal.clear(resource.handle,true,[&](uint64_t handle,bool){
                capture_coverage(d,immediate(cmd),reinterpret_cast<ID3D11Texture2D*>(handle),fusion->policy.clear);
            });
            if(d.boundary.resource==resource.handle)d.late_clear=true;
        }
        d.boundary.clear_resource(resource.handle,known_clear);
        it->second.cleared=count==0 && *depth==fusion->policy.clear;
    }catch(const std::exception& e){auto& d=fusion->devices[cmd->get_device()];d.boundary.invalid=true;++fusion->invalid;fusion->error=std::string(e.what()).substr(0,160);}
    return false;
}
ComPtr<ID3D11Texture2D> current_depth(ID3D11DeviceContext* context,uint64_t expected) {
    ComPtr<ID3D11DepthStencilView> view;context->OMGetRenderTargets(0,nullptr,&view);
    if(!view)throw std::runtime_error("Fusion boundary lacks depth binding");
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    if(reinterpret_cast<uint64_t>(resource.Get())!=expected)throw std::runtime_error("Fusion native depth identity changed");
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))throw std::runtime_error("Fusion depth is not a texture");
    return texture;
}
void sample(DepthSampler& sampler,ID3D11DeviceContext* context,ID3D11Texture2D* texture,ComPtr<ID3D11ShaderResourceView>& view) {
    auto* previous=sampler.output();D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    sampler.prepare(desc);sampler.sample(context,texture);
    if(!view || previous!=sampler.output()) {
        ComPtr<ID3D11Device> device;context->GetDevice(&device);view.Reset();
        if(FAILED(device->CreateShaderResourceView(sampler.output(),nullptr,&view)))throw std::runtime_error("Fusion owned depth SRV failed");
    }
}
void capture_coverage(WorldFusion::Impl::Device& d,ID3D11DeviceContext* context,ID3D11Texture2D* source,float clear){
    if(!d.coverage || !d.world)throw std::runtime_error("Missing world coverage baseline");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    const auto handle=reinterpret_cast<uint64_t>(source);
    const auto found=d.candidates.find(handle);
    if(found==d.candidates.end())throw std::runtime_error("Untracked coverage resource");
    if(handle==d.boundary.resource){
        if(!d.final)d.final=std::make_unique<DepthSampler>(device.Get());
        sample(*d.final,context,source,d.final_view);
        d.coverage->accumulate(context,d.world->output(),d.final->output(),d.journal.cleared(handle),clear);
    }else{
        if(!d.auxiliary)d.auxiliary=std::make_unique<DepthSampler>(device.Get());
        sample(*d.auxiliary,context,source,d.auxiliary_view);
        d.coverage->accumulate(context,d.world->output(),d.auxiliary->output(),d.journal.cleared(handle),clear,true);
    }
}
bool draw(api::command_list* cmd,uint32_t,uint32_t,uint32_t,uint32_t) {
    auto* context=immediate(cmd);if(!context)return false;
    std::lock_guard lock(fusion->mutex);
    try {
        auto& d=fusion->devices[cmd->get_device()];
        if(d.effects)return false;
        ComPtr<ID3D11DepthStencilState> state;UINT ref{};context->OMGetDepthStencilState(&state,&ref);
        D3D11_DEPTH_STENCIL_DESC description{};
        if(state)state->GetDesc(&description);else {description.DepthEnable=TRUE;description.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;}
        const bool writes=description.DepthEnable && description.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL;
        if(writes && d.unsupported_depth && d.boundary.ready())reject_coverage(d,"Unsupported post-world depth write");
        if(writes && d.bound && d.boundary.ready())d.journal.write(d.bound);
        UINT count=16;std::array<D3D11_VIEWPORT,16> v{};context->RSGetViewports(&count,v.data());
        if(count!=1 || v[0].TopLeftX!=0 || v[0].TopLeftY!=0 || v[0].Width!=float(d.width) || v[0].Height!=float(d.height)) {
            // Post-world coverage includes writes from smaller viewports too.
            if(writes && d.bound && d.boundary.armed && !d.boundary.latched){d.boundary.invalid=true;d.boundary.rejection=4;}return false;
        }
        const auto& vp=v[0];const std::array<double,6> viewport{vp.TopLeftX,vp.TopLeftY,vp.Width,vp.Height,vp.MinDepth,vp.MaxDepth};
        trace(d,2,viewport,0,0,writes);
        if(!writes || !d.bound)return false;
        auto& candidate=d.candidates[d.bound];++candidate.draws;
        if(!d.boundary.armed && !d.boundary.invalid && candidate.cleared && candidate.draws==fusion->policy.camera_draw
            && vp.MinDepth==fusion->policy.world[0] && vp.MaxDepth==fusion->policy.world[1]) {
            ComPtr<ID3D11Device> device;context->GetDevice(&device);
            if(!d.cameras)d.cameras=std::make_unique<CameraReadback>(device.Get(),fusion->layout);
            const auto seq=++fusion->sequence;
            if(d.cameras->enqueue(context,seq,monotonic_ns(),viewport)) {
                if(!d.matrices)d.matrices=std::make_unique<CameraGpu>(device.Get(),fusion->layout);
                d.matrices->capture(context);
                d.boundary.arm(d.bound,seq,viewport);d.current_sequence=seq;d.pending[seq]=false;++fusion->snapshots;
            }
        } else if(d.boundary.observe(d.bound,viewport,fusion->policy)) {
            auto source=current_depth(context,d.bound);
            ComPtr<ID3D11Device> device;context->GetDevice(&device);
            if(!d.world)d.world=std::make_unique<DepthSampler>(device.Get());
            sample(*d.world,context,source.Get(),d.world_view);
            if(!d.coverage)d.coverage=std::make_unique<DepthCoverage>(device.Get());
            d.coverage->reset(context,d.world->output());++fusion->boundaries;
            // This callback precedes the first foreground draw. Preserve it
            // even when the very next callback changes or clears its resource.
            d.journal.begin(d.bound);
        }
    }catch(const std::exception& e) {
        auto& d=fusion->devices[cmd->get_device()];d.boundary.invalid=true;++fusion->invalid;fusion->error=std::string(e.what()).substr(0,160);
    }
    return false;
}
bool indexed(api::command_list* c,uint32_t n,uint32_t i,uint32_t,int32_t,uint32_t){return draw(c,n,i,0,0);}
bool indirect(api::command_list* c,api::indirect_command type,api::resource,uint64_t,uint32_t n,uint32_t) {
    if(type==api::indirect_command::draw || type==api::indirect_command::draw_indexed)return draw(c,n,0,0,0);
    return false;
}
bool copy_depth(api::command_list* cmd,api::resource source,api::resource destination){
    auto* context=immediate(cmd);if(!context)return false;
    std::lock_guard lock(fusion->mutex);auto* device=cmd->get_device();auto& d=fusion->devices[device];
    if(d.effects)return false;
    try{
        // A depth destination need not have been bound as a DSV yet. Observe
        // its public descriptor before allowing this overwrite through.
        const auto desc=destination.handle?device->get_resource_desc(destination):api::resource_desc{};
        const bool depth=(desc.usage & api::resource_usage::depth_stencil)!=api::resource_usage::undefined;
        if(d.boundary.ready() && depth){
            if(!eligible(device,destination,d))reject_coverage(d,"Unsupported post-world depth copy");
            if(!d.candidates.contains(destination.handle) && d.candidates.size()>=16)
                reject_coverage(d,"Too many depth copy destinations");
            auto [it,inserted]=d.candidates.try_emplace(destination.handle);
            if(inserted)it->second.id=++d.next_resource_id;
            d.journal.copy(destination.handle,[&](uint64_t handle,bool){
                capture_coverage(d,context,reinterpret_cast<ID3D11Texture2D*>(handle),fusion->policy.clear);
            });
            if(destination.handle==d.boundary.resource)d.late_clear=false;
        }
        const auto a=d.candidates.find(source.handle),b=d.candidates.find(destination.handle);
        if(a!=d.candidates.end() || b!=d.candidates.end())
            trace(d,5,{},a==d.candidates.end()?0:a->second.id,b==d.candidates.end()?0:b->second.id);
    }catch(const std::exception& e){
        d.boundary.invalid=true;++fusion->invalid;fusion->error=std::string(e.what()).substr(0,160);
    }
    return false;
}
bool copy_region(api::command_list* cmd,api::resource source,uint32_t,const api::subresource_box*,api::resource destination,uint32_t,const api::subresource_box*,api::filter_mode){return copy_depth(cmd,source,destination);}
void end(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(!fusion)return;
    std::lock_guard lock(fusion->mutex);auto& d=fusion->devices[runtime->get_device()];d.effects=false;d.bound=0;
}
void reset(api::command_list* cmd) {
    if(!immediate(cmd))return;
    std::lock_guard lock(fusion->mutex);auto& d=fusion->devices[cmd->get_device()];d.bound=0;d.boundary.invalid=true;d.boundary.rejection=5;
}
void secondary(api::command_list* cmd,api::command_list*){reset(cmd);}
void destroy_resource(api::device* device,api::resource resource) {
    if(!fusion)return;
    std::lock_guard lock(fusion->mutex);const auto found=fusion->devices.find(device);if(found==fusion->devices.end())return;
    auto& d=found->second;
    if(d.boundary.ready() && d.journal.contains(resource.handle)){d.boundary.invalid=true;d.boundary.rejection=3;}
    d.candidates.erase(resource.handle);d.boundary.clear_resource(resource.handle);
    d.colors.erase(resource.handle);if(d.color==resource.handle)d.color=0;
    if(d.bound==resource.handle)d.bound=0;
}
void destroy_device(api::device* device) {
    if(!fusion)return;
    std::lock_guard lock(fusion->mutex);fusion->devices.erase(device);
}
}
WorldFusion::WorldFusion(bool e,const std::filesystem::path& base,Receiver& receiver):impl_(std::make_unique<Impl>(e,base,receiver)){}
WorldFusion::~WorldFusion(){uninstall();}
void WorldFusion::install() {
    if(!impl_->enabled || impl_->installed)return;
    fusion=impl_.get();impl_->installed=true;
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind_depth);
    reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(clear_depth);
    reshade::register_event<reshade::addon_event::draw>(draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::register_event<reshade::addon_event::copy_resource>(copy_depth);
    reshade::register_event<reshade::addon_event::copy_texture_region>(copy_region);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::register_event<reshade::addon_event::reset_command_list>(reset);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::register_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::register_event<reshade::addon_event::destroy_device>(destroy_device);
}
void WorldFusion::uninstall() {
    if(!impl_->installed)return;
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind_depth);
    reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(clear_depth);
    reshade::unregister_event<reshade::addon_event::draw>(draw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::unregister_event<reshade::addon_event::copy_resource>(copy_depth);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(copy_region);
    reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::unregister_event<reshade::addon_event::reset_command_list>(reset);
    reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(destroy_device);
    fusion=nullptr;impl_->installed=false;impl_->devices.clear();
}
void WorldFusion::begin(api::effect_runtime* runtime,api::command_list* cmd) {
    if(!impl_->enabled)return;
    release(runtime);
    std::lock_guard lock(impl_->mutex);auto& d=impl_->devices[runtime->get_device()];d.effects=true;d.available=false;d.camera.reset();
    try {
        auto* context=immediate(cmd);
        if(!context)throw std::runtime_error("Fusion requires an immediate D3D11 context");
        // Finish every dirty destination before accepting this camera/depth
        // interval. The final bound DSV alone does not cover unbound copies.
        if(d.boundary.ready()){
            auto capture=[&](uint64_t handle,bool){capture_coverage(d,context,reinterpret_cast<ID3D11Texture2D*>(handle),impl_->policy.clear);};
            d.journal.capture(d.boundary.resource,capture,true);
            d.journal.flush(capture);
        }
        if(d.current_sequence)d.pending[d.current_sequence]=d.boundary.ready();
        if(d.boundary.invalid)impl_->last_rejection=d.boundary.rejection;
        if(d.boundary.armed) {
            impl_->last_transitions=d.transitions;impl_->last_transition_count=d.transition_count;
            impl_->last_trace_truncated=d.trace_truncated;
        }
        d.output_viewport=d.boundary.viewport;
        d.output_late_clear=d.late_clear;
        d.available=d.boundary.ready() && bool(d.matrices) && bool(d.world_view);
        if(d.available)++impl_->gpu_pairs;
        if(d.cameras)while(auto camera=d.cameras->poll(context)) {
            const auto found=d.pending.find(camera->sequence);
            const bool paired=found!=d.pending.end() && found->second;
            if(found!=d.pending.end())d.pending.erase(found);
            if(!paired)continue;
            const auto decoded=camera->decode(impl_->layout);
            impl_->last_camera=decoded;
            impl_->receiver.submit_camera(decoded,camera->sequence,camera->captured_ns);
            if(camera->sequence==d.current_sequence && d.boundary.ready()) {d.camera=decoded;++impl_->paired;}
        }
        if(!d.available)++impl_->not_ready;
    }catch(const std::exception& e){
        d.available=false;d.boundary.invalid=true;d.boundary.rejection=6;
        if(d.current_sequence)d.pending[d.current_sequence]=false;
        impl_->last_rejection=d.boundary.rejection;++impl_->invalid;impl_->error=std::string(e.what()).substr(0,160);
    }
    d.boundary.reset();d.bound=0;d.current_sequence=0;d.late_clear=false;d.journal.reset();d.unsupported_depth=false;
    d.transition_count=0;d.trace_truncated=false;d.last_draw={};d.last_bind={};
    for(auto& [_,candidate]:d.candidates){candidate.draws=0;candidate.cleared=false;}
    d.backbuffer=runtime->get_current_back_buffer().handle;
    uint32_t width{},height{};runtime->get_screenshot_width_and_height(&width,&height);
    if(d.width!=width || d.height!=height){d.available=false;d.candidates.clear();d.pending.clear();d.cameras.reset();d.matrices.reset();d.world_view.Reset();d.final_view.Reset();d.world.reset();d.final.reset();d.coverage.reset();d.auxiliary.reset();d.auxiliary_view.Reset();}
    d.width=width;d.height=height;
}
void WorldFusion::release(api::effect_runtime* runtime) {
    if(!impl_->enabled)return;
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_WORLD",{},{});
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_FINAL",{},{});
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_CAMERA",{},{});
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_COVERAGE",{},{});
}
bool WorldFusion::bind(api::effect_runtime* runtime,const Frame& frame) {
    if(!impl_->enabled)return false;
    const auto diagnostic=runtime->find_uniform_variable("CounterCraftProbe.fx","CCFusionDiagnostic");
    if(diagnostic.handle)runtime->set_uniform_value_uint(diagnostic,impl_->policy.diagnostic_view);
    std::lock_guard lock(impl_->mutex);const auto found=impl_->devices.find(runtime->get_device());
    if(found==impl_->devices.end())return false;
    auto& d=found->second;
    if(!d.available || !d.matrices || !d.world_view || !d.final_view || !d.coverage){release(runtime);return false;}
    if(!frame.relayed_camera || !fusion_aligned(frame,*frame.relayed_camera) || frame.relayed_camera->viewport!=d.output_viewport) {
        ++impl_->unaligned;release(runtime);return false;
    }
    // The effect checks all current GPU matrices against this decoded and
    // rendered-pose-validated camera. CPU query latency cannot accept stale rays.
    const auto& c=*frame.relayed_camera;const auto& p=c.projection;
    auto set=[&](const char* name,const std::array<float,4>& v){auto u=runtime->find_uniform_variable("CounterCraftProbe.fx",name);if(u.handle)runtime->set_uniform_value_float(u,v.data(),4);};
    set("CCGuestPlanes",{float(frame.metadata.near_plane),float(frame.metadata.far_plane),float(frame.metadata.projection[0]/p[0]),float(frame.metadata.projection[5]/p[5])});
    set("CCHostProjection",{float(p[10]),float(p[11]),float(p[14]),float(p[15])});
    set("CCHostRange",{float(c.viewport[4]),float(c.viewport[5]),impl_->policy.clear,1.f/32});
    std::array<float,64> expected{};auto relative=c.view;relative[3]=relative[7]=relative[11]=0;
    for(size_t r=0;r<4;++r)for(size_t k=0;k<4;++k) {
        expected[r*4+k]=float(c.view[r*4+k]);expected[16+r*4+k]=float(p[r*4+k]);
        double world=0,rotation=0;for(size_t j=0;j<4;++j){world+=p[r*4+j]*c.view[j*4+k];rotation+=p[r*4+j]*relative[j*4+k];}
        expected[32+r*4+k]=float(world);expected[48+r*4+k]=float(rotation);
    }
    const auto matrix=runtime->find_uniform_variable("CounterCraftProbe.fx","CCExpectedCamera");
    if(matrix.handle)runtime->set_uniform_value_float(matrix,expected.data(),expected.size());
    const auto reversed=runtime->find_uniform_variable("CounterCraftProbe.fx","CCHostReversed");
    if(reversed.handle)runtime->set_uniform_value_bool(reversed,c.reversed);
    const auto cleared=runtime->find_uniform_variable("CounterCraftProbe.fx","CCAllowFinalClear");
    if(cleared.handle)runtime->set_uniform_value_bool(cleared,d.output_late_clear);
    const api::resource_view world{reinterpret_cast<uint64_t>(d.world_view.Get())},final{reinterpret_cast<uint64_t>(d.final_view.Get())};
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_WORLD",world,world);
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_FINAL",final,final);
    const api::resource_view matrices{reinterpret_cast<uint64_t>(d.matrices->view())};
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_CAMERA",matrices,matrices);
    const api::resource_view coverage{reinterpret_cast<uint64_t>(d.coverage->view())};
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_COVERAGE",coverage,coverage);
    ++impl_->shown;return true;
}
nlohmann::json WorldFusion::report()const {
    if(!impl_->enabled)return {{"enabled",false}};
    std::lock_guard lock(impl_->mutex);
    auto transitions=nlohmann::json::array();
    for(size_t i=0;i<impl_->last_transition_count;++i){const auto& t=impl_->last_transitions[i];
        transitions.push_back({{"viewport",t.viewport},{"rejection",t.rejection},{"latched",t.latched},
            {"resource",t.resource},{"color",t.color},{"samples",t.samples},{"event",t.event},
            {"source",t.source},{"destination",t.destination},{"backbuffer",t.backbuffer},{"writesDepth",t.writes_depth}});}
    return {{"enabled",true},{"mode","static-pose-world-fusion"},{"cameraSnapshots",impl_->snapshots.load()},
        {"worldBoundaries",impl_->boundaries.load()},{"sameFrameCpuPairs",impl_->paired.load()},
        {"sameFrameGpuPairs",impl_->gpu_pairs.load()},{"effectEligibleFrames",impl_->shown.load()},
        {"poseMismatchFrames",impl_->unaligned.load()},{"notReadyFrames",impl_->not_ready.load()},{"invalid",impl_->invalid.load()},
        {"lastError",impl_->error},{"lastBoundaryRejection",impl_->last_rejection},
        {"lastDecodedCamera",impl_->last_camera?impl_->last_camera->report():nlohmann::json(nullptr)},
        {"lastViewportTransitions",transitions},{"traceTruncated",impl_->last_trace_truncated},{"liveOcclusionVerified",false}};
}
}
