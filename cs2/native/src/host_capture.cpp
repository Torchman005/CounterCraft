#include "host_capture.hpp"
#include "capture_control.hpp"
#include "depth_readback.hpp"
#include "depth_view.hpp"
#include <reshade.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cc {
namespace api=reshade::api;
struct HostCapture::Impl {
    struct Candidate { uint64_t id{},draws{}; ViewportCaptureSchedule schedule; };
    struct Device {
        std::unique_ptr<DepthReadback> readback;
        std::unordered_map<uint64_t,Candidate> candidates;
        uint64_t bound{},frame{},draws{};
        uint32_t width{},height{};
        bool effects{},capture_frame{};
        uint64_t request{};
    };
    bool enabled{},installed{};
    std::filesystem::path directory;
    std::filesystem::path control_path;
    bool manual{},viewport_transitions{};
    CaptureRequests requests;
    std::atomic<uint64_t> control_failures{};
    std::mutex gpu_mutex,queue_mutex,error_mutex;
    std::unordered_map<api::device*,Device> devices;
    std::deque<DepthCaptureFrame> queue;
    std::condition_variable wake;
    std::thread writer;
    bool ending{};
    std::string last_failure;
    std::atomic<uint64_t> queued{},written{},busy{},failures{},discarded{},cpu_max_us{},next_id{1},pending{};
    static constexpr uint64_t limit=18,period=240;
    explicit Impl(bool e,const std::filesystem::path& base):enabled(e) {
        if(e) {
            directory=base/"captures"/("session-"+std::to_string(GetTickCount64())); std::filesystem::create_directories(directory);
            control_path=base/"capture-control.json";
            if(std::filesystem::exists(control_path)) {
                if(std::filesystem::file_size(control_path)>4096) throw std::runtime_error("Capture control exceeds 4KiB");
                std::ifstream input(control_path); const auto config=nlohmann::json::parse(input);
                manual=config.value("manual",false);
                if(manual) requests.initialize(capture_sequence(config));
                viewport_transitions=capture_transitions(config);
            }
        }
    }
    void measure(std::chrono::steady_clock::time_point start) {
        const auto us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
        auto old=cpu_max_us.load(); while(old<us && !cpu_max_us.compare_exchange_weak(old,us)) {}
    }
    void recount() { size_t n=0; for(const auto& [_,d]:devices) if(d.readback) n+=d.readback->pending(); pending.store(n); }
    void failed(const char* why) {
        ++failures; std::lock_guard lock(error_mutex); last_failure=std::string(why).substr(0,200);
    }
    void write_completed(DepthCaptureFrame frame) noexcept {
        try {
            const auto path=directory/("capture-"+std::to_string(frame.metadata.at("captureId").get<uint64_t>()));
            std::filesystem::create_directories(path);
            for(const auto& f:frame.files) {
                std::ofstream output(path/f.name,std::ios::binary); output.exceptions(std::ios::badbit|std::ios::failbit);
                output.write(reinterpret_cast<const char*>(f.bytes.data()),std::streamsize(f.bytes.size()));
            }
            std::ofstream manifest(path/"capture.json"); manifest.exceptions(std::ios::badbit|std::ios::failbit); manifest<<frame.metadata.dump(2)<<'\n';
            ++written;
        } catch(...) { ++failures; }
    }
    void writer_loop() {
        for(;;) {
            DepthCaptureFrame frame;
            bool have_frame=false;
            { std::unique_lock lock(queue_mutex); wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return ending || !queue.empty();});
              if(ending && queue.empty()) return;
              if(!queue.empty()) { frame=std::move(queue.front()); queue.pop_front(); have_frame=true; } }
            if(manual) {
                // File IO stays on this private diagnostic worker. A request
                // arms ONE future host interval, and never resets the total cap.
                try {
                    if(std::filesystem::file_size(control_path)>4096) throw std::runtime_error("Capture control exceeds 4KiB");
                    std::ifstream input(control_path);
                    requests.observe(capture_sequence(nlohmann::json::parse(input)));
                } catch(...) { ++control_failures; }
            }
            if(!have_frame) continue;
            write_completed(std::move(frame));
        }
    }
};
namespace {
HostCapture::Impl* capture=nullptr;
using Microsoft::WRL::ComPtr;
bool eligible(api::device* device) { return capture && device && device->get_api()==api::device_api::d3d11; }
ID3D11DeviceContext* immediate(api::command_list* cmd) {
    if(!cmd || !eligible(cmd->get_device())) return nullptr;
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
    return context && context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE?context:nullptr;
}
void bind(api::command_list* cmd,uint32_t,const api::resource_view*,api::resource_view dsv) {
    if(!immediate(cmd)) return;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) { ++capture->busy; return; }
    try {
        auto* device=cmd->get_device(); auto& d=capture->devices[device]; d.bound=0;
        if(d.effects || !dsv.handle) return;
        const auto resource=device->get_resource_from_view(dsv); if(!resource.handle) return;
        const auto desc=device->get_resource_desc(resource);
        if(desc.type!=api::resource_type::texture_2d || desc.texture.width!=d.width || desc.texture.height!=d.height
            || desc.texture.depth_or_layers!=1 || desc.texture.levels!=1 || desc.texture.samples>8) return;
        const auto view=device->get_resource_view_desc(dsv);
        if(view.type!=api::resource_view_type::texture_2d && view.type!=api::resource_view_type::texture_2d_multisample) return;
        if(view.type==api::resource_view_type::texture_2d && view.texture.first_level!=0) return;
        if(!d.candidates.contains(resource.handle)) {
            if(d.candidates.size()>=16) { ++capture->busy; return; }
            d.candidates.emplace(resource.handle,HostCapture::Impl::Candidate{capture->next_id.fetch_add(1),0});
        }
        d.bound=resource.handle;
    } catch(...) { ++capture->failures; }
}
void destroy_resource(api::device* device,api::resource resource) {
    if(!eligible(device)) return;
    // Releases of our staging resources may reenter this callback.
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) return;
    const auto it=capture->devices.find(device); if(it==capture->devices.end()) return;
    it->second.candidates.erase(resource.handle); if(it->second.bound==resource.handle) it->second.bound=0;
}
void destroy_device(api::device* device) {
    if(!eligible(device)) return;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) { ++capture->failures; return; }
    const auto it=capture->devices.find(device);
    if(it!=capture->devices.end()) {
        if(it->second.readback) capture->discarded+=it->second.readback->pending();
        capture->devices.erase(it); capture->recount();
    }
}
bool draw(api::command_list* cmd,uint32_t elements,uint32_t instances,uint32_t,uint32_t) {
    auto* context=immediate(cmd); if(!context) return false;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) { ++capture->busy; return false; }
    try {
        const auto found=capture->devices.find(cmd->get_device()); if(found==capture->devices.end()) return false;
        auto& d=found->second; ++d.draws;
        if(d.effects || !d.bound || !d.capture_frame
            || capture->queued>=HostCapture::Impl::limit || capture->failures>=3) return false;
        const auto candidate=d.candidates.find(d.bound); if(candidate==d.candidates.end()) return false;
        auto& c=candidate->second; ++c.draws;
        UINT n=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; std::array<D3D11_VIEWPORT,16> v{};
        std::optional<ViewportCaptureSchedule::Viewport> previous;
        if(capture->viewport_transitions) {
            context->RSGetViewports(&n,v.data());
            if(n!=1) { c.schedule.reset(); return false; }
            previous=c.schedule.previous;
            if(!c.schedule.observe(c.draws,{v[0].TopLeftX,v[0].TopLeftY,v[0].Width,v[0].Height,v[0].MinDepth,v[0].MaxDepth})) return false;
        } else {
            // Three distinct pre-draw points, not a guessed end-of-world pass.
            if(c.draws!=64 && c.draws!=256 && c.draws!=512) return false;
            context->RSGetViewports(&n,v.data());
        }
        const auto start=std::chrono::steady_clock::now();
        ComPtr<ID3D11DepthStencilView> dsv; context->OMGetRenderTargets(0,nullptr,&dsv); if(!dsv) return false;
        ComPtr<ID3D11Resource> resource; dsv->GetResource(&resource);
        if(reinterpret_cast<uintptr_t>(resource.Get())!=d.bound) { capture->failed("Native depth differs from observed binding"); return false; }
        ComPtr<ID3D11Texture2D> depth; if(FAILED(resource.As(&depth))) return false;
        ComPtr<ID3D11Device> native; context->GetDevice(&native);
        if(!d.readback) d.readback=std::make_unique<DepthReadback>(native.Get());
        nlohmann::json metadata={{"schema",1},{"frame",d.frame},{"candidateId",c.id},{"candidateDraw",c.draws},
            {"deviceDraw",d.draws},{"elements",elements},{"instances",instances},{"timing","before-current-draw"},
            {"cameraDepthVerified",false},{"depthConvention","unverified"},{"outputSize",{d.width,d.height}}};
        metadata["viewports"]=nlohmann::json::array();
        for(UINT i=0;i<n;++i) metadata["viewports"].push_back({v[i].TopLeftX,v[i].TopLeftY,v[i].Width,v[i].Height,v[i].MinDepth,v[i].MaxDepth});
        if(capture->viewport_transitions) {
            metadata["trigger"]="viewport-transitions";
            metadata["viewportTransitions"]=c.schedule.transitions;
            metadata["previousViewport"]=previous?nlohmann::json(*previous):nlohmann::json(nullptr);
            // Current bindings belong to the new draw. A preceding viewport is
            // evidence about prior draws, not the new buffer's camera convention.
            metadata["bindingTiming"]="current-draw-after-viewport-change-or-draw64";
        }
        ComPtr<ID3D11DepthStencilState> state; UINT ref=0; context->OMGetDepthStencilState(&state,&ref);
        D3D11_DEPTH_STENCIL_DESC description{};
        if(state) state->GetDesc(&description);
        else { description.DepthEnable=TRUE; description.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; description.DepthFunc=D3D11_COMPARISON_LESS; }
        metadata["depthState"]={{"enabled",bool(description.DepthEnable)},{"writeMask",uint32_t(description.DepthWriteMask)},
            {"comparison",uint32_t(description.DepthFunc)},{"stencilRef",ref}};
        metadata["captureId"]=capture->queued.load()+1;
        metadata["request"]=capture->manual?nlohmann::json(d.request):nlohmann::json(nullptr);
        if(d.readback->enqueue(context,depth.Get(),std::move(metadata))) ++capture->queued; else ++capture->busy;
        capture->recount(); capture->measure(start);
    } catch(const std::exception& e) { capture->failed(e.what()); } catch(...) { capture->failed("Unknown draw capture failure"); }
    return false;
}
bool indexed(api::command_list* c,uint32_t n,uint32_t i,uint32_t,int32_t,uint32_t) { return draw(c,n,i,0,0); }
bool indirect(api::command_list* c,api::indirect_command type,api::resource,uint64_t,uint32_t n,uint32_t) {
    if(type==api::indirect_command::draw || type==api::indirect_command::draw_indexed) return draw(c,n,0,0,0);
    return false;
}
void begin(api::effect_runtime* runtime,api::command_list* cmd,api::resource_view,api::resource_view) {
    auto* context=immediate(cmd); if(!context) return;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) { ++capture->busy; return; }
    try {
        auto& d=capture->devices[runtime->get_device()]; d.effects=true; d.bound=0;
        ++d.frame; d.draws=0; for(auto& [_,c]:d.candidates) { c.draws=0; c.schedule.reset(); }
        d.capture_frame=false;
        if(capture->manual) {
            if(const auto request=capture->requests.take(capture->queued<HostCapture::Impl::limit && capture->failures<3)) {
                // Consume exactly one sequence value per effect interval. This
                // preserves queued operator requests when the file is updated
                // faster than the host reaches a new interval.
                d.request=*request; d.capture_frame=true;
            }
        } else d.capture_frame=d.frame>=240 && d.frame%HostCapture::Impl::period==0;
        runtime->get_screenshot_width_and_height(&d.width,&d.height);
        if(d.readback && d.readback->pending()) {
            std::unique_lock queue_lock(capture->queue_mutex,std::try_to_lock);
            if(queue_lock && capture->queue.size()<DepthReadback::capacity) {
                const auto start=std::chrono::steady_clock::now();
                if(auto frame=d.readback->poll(context)) { capture->queue.push_back(std::move(*frame)); capture->wake.notify_one(); }
                capture->measure(start); capture->recount();
            } else ++capture->busy;
        }
    } catch(...) { ++capture->failures; }
}
void end(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(!eligible(runtime->get_device())) return;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(!lock) { ++capture->busy; return; }
    auto& d=capture->devices[runtime->get_device()]; d.effects=false; d.bound=0;
}
void reset(api::command_list* cmd) {
    if(!cmd || !eligible(cmd->get_device())) return;
    std::unique_lock lock(capture->gpu_mutex,std::try_to_lock); if(lock) capture->devices[cmd->get_device()].bound=0;
}
void secondary(api::command_list* c,api::command_list*) { reset(c); }
}
HostCapture::HostCapture(bool enabled,const std::filesystem::path& path):impl_(std::make_unique<Impl>(enabled,path)) {}
HostCapture::~HostCapture() { uninstall(); }
void HostCapture::install() {
    if(!impl_->enabled || impl_->installed) return; capture=impl_.get(); impl_->installed=true;
    impl_->ending=false; impl_->writer=std::thread(&Impl::writer_loop,impl_.get());
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind);
    reshade::register_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::register_event<reshade::addon_event::destroy_device>(destroy_device);
    reshade::register_event<reshade::addon_event::draw>(draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::register_event<reshade::addon_event::reset_command_list>(reset);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(secondary);
}
void HostCapture::uninstall() {
    if(!impl_->installed) return;
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(destroy_device);
    reshade::unregister_event<reshade::addon_event::draw>(draw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::unregister_event<reshade::addon_event::reset_command_list>(reset);
    reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    capture=nullptr; impl_->installed=false;
    for(const auto& [_,d]:impl_->devices) if(d.readback) impl_->discarded+=d.readback->pending();
    impl_->devices.clear(); impl_->pending.store(0);
    { std::lock_guard lock(impl_->queue_mutex); impl_->ending=true; }
    impl_->wake.notify_one(); if(impl_->writer.joinable()) impl_->writer.join();
}
nlohmann::json HostCapture::report() const {
    if(!impl_->enabled) return {{"enabled",false}};
    std::lock_guard lock(impl_->error_mutex);
    return {{"enabled",true},{"limit",Impl::limit},{"periodFrames",Impl::period},
        {"manual",impl_->manual},{"requested",impl_->requests.requested.load()},{"consumed",impl_->requests.consumed.load()},
        {"controlFailures",impl_->control_failures.load()},
        {"trigger",impl_->viewport_transitions?"viewport-transitions":"draw-milestones"},
        {"queued",impl_->queued.load()},{"written",impl_->written.load()},{"gpuPending",impl_->pending.load()},
        {"busySkips",impl_->busy.load()},{"failures",impl_->failures.load()},{"discardedPending",impl_->discarded.load()},
        {"maxCallbackUs",impl_->cpu_max_us.load()},{"lastFailure",impl_->last_failure},{"cameraDepthVerified",false}};
}
}
