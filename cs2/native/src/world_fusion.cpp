#include "world_fusion.hpp"
#include "world_boundary.hpp"
#include "camera_readback.hpp"
#include "camera_gpu.hpp"
#include "depth_sampler.hpp"
#include "receiver.hpp"
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace cc {
namespace api=reshade::api;
using Microsoft::WRL::ComPtr;
struct WorldFusion::Impl {
    struct Candidate {uint64_t draws{};bool cleared{};};
    struct Device {
        uint32_t width{},height{};
        uint64_t bound{},current_sequence{};
        bool effects{},available{};
        bool late_clear{},output_late_clear{};
        WorldBoundary boundary;
        std::unordered_map<uint64_t,Candidate> candidates;
        std::unordered_map<uint64_t,bool> pending;
        std::unique_ptr<CameraReadback> cameras;
        std::unique_ptr<CameraGpu> matrices;
        std::unique_ptr<DepthSampler> world,final;
        ComPtr<ID3D11ShaderResourceView> world_view,final_view;
        std::optional<HostCamera> camera;
        std::array<double,6> output_viewport{};
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
void bind_depth(api::command_list* cmd,uint32_t,const api::resource_view*,api::resource_view view) {
    if(!immediate(cmd))return;
    std::lock_guard lock(fusion->mutex);
    try {
        auto* device=cmd->get_device();auto& d=fusion->devices[device];d.bound=0;
        if(d.effects || !view.handle)return;
        const auto resource=device->get_resource_from_view(view);
        if(!eligible(device,resource,d))return;
        if(!d.candidates.contains(resource.handle) && d.candidates.size()>=16)return;
        d.candidates.try_emplace(resource.handle);d.bound=resource.handle;
    }catch(...){++fusion->invalid;}
}
bool clear_depth(api::command_list* cmd,api::resource_view view,const float* depth,const uint8_t*,uint32_t count,const api::rect*) {
    if(!immediate(cmd) || !depth)return false;
    std::lock_guard lock(fusion->mutex);
    try {
        auto* device=cmd->get_device();auto& d=fusion->devices[device];
        if(d.effects)return false;
        const auto resource=device->get_resource_from_view(view);
        if(!eligible(device,resource,d))return false;
        const bool known_clear=count==0 && *depth==fusion->policy.clear;
        if(d.boundary.latched && d.boundary.resource==resource.handle && known_clear)d.late_clear=true;
        d.boundary.clear_resource(resource.handle,known_clear);
        if(!d.candidates.contains(resource.handle) && d.candidates.size()>=16)return false;
        d.candidates[resource.handle].cleared=count==0 && *depth==fusion->policy.clear;
    }catch(...){++fusion->invalid;}
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
bool draw(api::command_list* cmd,uint32_t,uint32_t,uint32_t,uint32_t) {
    auto* context=immediate(cmd);if(!context)return false;
    std::lock_guard lock(fusion->mutex);
    try {
        auto& d=fusion->devices[cmd->get_device()];
        if(d.effects || !d.bound)return false;
        ComPtr<ID3D11DepthStencilState> state;UINT ref{};context->OMGetDepthStencilState(&state,&ref);
        D3D11_DEPTH_STENCIL_DESC description{};
        if(state)state->GetDesc(&description);else {description.DepthEnable=TRUE;description.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;}
        if(!description.DepthEnable || description.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ALL)return false;
        UINT count=16;std::array<D3D11_VIEWPORT,16> v{};context->RSGetViewports(&count,v.data());
        if(count!=1 || v[0].TopLeftX!=0 || v[0].TopLeftY!=0 || v[0].Width!=float(d.width) || v[0].Height!=float(d.height)) {
            // Later small viewports (HUD/shadows) cannot redefine a world
            // snapshot. Final-depth comparison still protects their depth writes.
            if(d.boundary.armed && !d.boundary.latched){d.boundary.invalid=true;d.boundary.rejection=4;}return false;
        }
        const auto& vp=v[0];const std::array<double,6> viewport{vp.TopLeftX,vp.TopLeftY,vp.Width,vp.Height,vp.MinDepth,vp.MaxDepth};
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
            sample(*d.world,context,source.Get(),d.world_view);++fusion->boundaries;
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
    auto& d=found->second;d.candidates.erase(resource.handle);d.boundary.clear_resource(resource.handle);
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
        if(d.current_sequence)d.pending[d.current_sequence]=d.boundary.ready();
        if(d.boundary.invalid)impl_->last_rejection=d.boundary.rejection;
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
            impl_->receiver.submit_camera(decoded,camera->sequence,camera->captured_ns);
            if(camera->sequence==d.current_sequence && d.boundary.ready()) {d.camera=decoded;++impl_->paired;}
        }
        if(d.available) {
            // Only an observed public resource handle, invalidated by its destroy
            // callback. We keep no host COM reference between callbacks.
            auto* source=reinterpret_cast<ID3D11Texture2D*>(d.boundary.resource);
            ComPtr<ID3D11Device> device;context->GetDevice(&device);
            if(!d.final)d.final=std::make_unique<DepthSampler>(device.Get());
            sample(*d.final,context,source,d.final_view);
        }else ++impl_->not_ready;
        impl_->error.clear();
    }catch(const std::exception& e){d.available=false;++impl_->invalid;impl_->error=std::string(e.what()).substr(0,160);}
    d.boundary.reset();d.bound=0;d.current_sequence=0;d.late_clear=false;
    for(auto& [_,candidate]:d.candidates)candidate={};
    uint32_t width{},height{};runtime->get_screenshot_width_and_height(&width,&height);
    if(d.width!=width || d.height!=height){d.available=false;d.candidates.clear();d.pending.clear();d.cameras.reset();d.matrices.reset();d.world_view.Reset();d.final_view.Reset();d.world.reset();d.final.reset();}
    d.width=width;d.height=height;
}
void WorldFusion::release(api::effect_runtime* runtime) {
    if(!impl_->enabled)return;
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_WORLD",{},{});
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_FINAL",{},{});
    runtime->update_texture_bindings("COUNTERCRAFT_HOST_CAMERA",{},{});
}
bool WorldFusion::bind(api::effect_runtime* runtime,const Frame& frame) {
    if(!impl_->enabled)return false;
    std::lock_guard lock(impl_->mutex);const auto found=impl_->devices.find(runtime->get_device());
    if(found==impl_->devices.end())return false;
    auto& d=found->second;
    if(!d.available || !d.matrices || !d.world_view || !d.final_view){release(runtime);return false;}
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
    ++impl_->shown;return true;
}
nlohmann::json WorldFusion::report()const {
    if(!impl_->enabled)return {{"enabled",false}};
    std::lock_guard lock(impl_->mutex);
    return {{"enabled",true},{"mode","static-pose-world-fusion"},{"cameraSnapshots",impl_->snapshots.load()},
        {"worldBoundaries",impl_->boundaries.load()},{"sameFrameCpuPairs",impl_->paired.load()},
        {"sameFrameGpuPairs",impl_->gpu_pairs.load()},{"effectEligibleFrames",impl_->shown.load()},
        {"poseMismatchFrames",impl_->unaligned.load()},{"notReadyFrames",impl_->not_ready.load()},{"invalid",impl_->invalid.load()},
        {"lastError",impl_->error},{"lastBoundaryRejection",impl_->last_rejection},{"liveOcclusionVerified",false}};
}
}
