#include "host_camera_feed.hpp"
#include "camera_readback.hpp"
#include "receiver.hpp"
#include <reshade.hpp>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cc {
namespace api=reshade::api;
using Microsoft::WRL::ComPtr;
struct HostCameraFeed::Impl {
    struct Device {
        std::unique_ptr<CameraReadback> readback;
        uint32_t width{},height{};
        uint64_t bound{};
        int64_t next_capture{};
        std::unordered_map<uint64_t,uint64_t> draws;
        bool effects{};
    };
    bool enabled{},installed{},ending{};
    Receiver& receiver;
    CameraLayout layout;
    std::mutex gpu_mutex,worker_mutex;
    std::unordered_map<api::device*,Device> devices;
    std::optional<CameraSample> mailbox;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<uint64_t> sequence{},queued{},decoded{},invalid{},busy{},pending{},replaced{};
    nlohmann::json last_camera;
    std::string error;
    Impl(bool e,const std::filesystem::path& base,Receiver& r):enabled(e),receiver(r) {
        if(!e)return;
        const auto path=base/"camera-layout.json";
        if(std::filesystem::file_size(path)>65536)throw std::runtime_error("Camera calibration exceeds 64KiB");
        std::ifstream input(path);layout=CameraLayout::parse(nlohmann::json::parse(input));
    }
    void recount() {size_t count=0;for(const auto& [_,d]:devices)if(d.readback)count+=d.readback->pending();pending=count;}
    void loop() {
        for(;;) {
            CameraSample sample;
            {std::unique_lock lock(worker_mutex);wake.wait(lock,[&]{return ending || mailbox.has_value();});
             if(ending)return; sample=std::move(*mailbox);mailbox.reset();}
            try {
                const auto camera=sample.decode(layout);
                receiver.submit_camera(camera,sample.sequence,sample.captured_ns);
                std::lock_guard lock(worker_mutex);last_camera=camera.report();
                last_camera["sequence"]=sample.sequence;last_camera["capturedNanos"]=sample.captured_ns;
                error.clear();++decoded;
            }catch(const std::exception& e){++invalid;std::lock_guard lock(worker_mutex);error=std::string(e.what()).substr(0,160);}
        }
    }
};
namespace {
HostCameraFeed::Impl* feed=nullptr;
ID3D11DeviceContext* immediate(api::command_list* cmd) {
    if(!feed || !cmd || cmd->get_device()->get_api()!=api::device_api::d3d11)return nullptr;
    auto* c=reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
    return c && c->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE?c:nullptr;
}
void bind(api::command_list* cmd,uint32_t,const api::resource_view*,api::resource_view dsv) {
    if(!immediate(cmd))return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock){++feed->busy;return;}
    try {
        auto* device=cmd->get_device();auto& d=feed->devices[device];d.bound=0;
        if(d.effects || !dsv.handle)return;
        const auto resource=device->get_resource_from_view(dsv);if(!resource.handle)return;
        const auto desc=device->get_resource_desc(resource);const auto view=device->get_resource_view_desc(dsv);
        if(desc.type!=api::resource_type::texture_2d || desc.texture.width!=d.width || desc.texture.height!=d.height
            || desc.texture.depth_or_layers!=1 || desc.texture.levels!=1 || desc.texture.samples>8
            || (view.type!=api::resource_view_type::texture_2d && view.type!=api::resource_view_type::texture_2d_multisample))return;
        if(!d.draws.contains(resource.handle) && d.draws.size()>=16)return;
        d.draws.try_emplace(resource.handle,0);d.bound=resource.handle;
    }catch(...){++feed->invalid;}
}
bool draw(api::command_list* cmd,uint32_t,uint32_t,uint32_t,uint32_t) {
    auto* context=immediate(cmd);if(!context)return false;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock){++feed->busy;return false;}
    try {
        auto& d=feed->devices[cmd->get_device()];
        if(d.effects || !d.bound || ++d.draws[d.bound]!=64)return false;
        const auto now=monotonic_ns();
        if(now<d.next_capture)return false;
        d.next_capture=now+33'333'333;
        UINT count=16;std::array<D3D11_VIEWPORT,16> viewport{};context->RSGetViewports(&count,viewport.data());
        if(count!=1 || viewport[0].Width!=float(d.width) || viewport[0].Height!=float(d.height))return false;
        ComPtr<ID3D11Device> device;context->GetDevice(&device);
        if(!d.readback)d.readback=std::make_unique<CameraReadback>(device.Get(),feed->layout);
        const auto& v=viewport[0];
        if(d.readback->enqueue(context,++feed->sequence,now,{v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth}))++feed->queued;
        else ++feed->busy;
        feed->recount();
    }catch(...){++feed->invalid;}
    return false;
}
bool indexed(api::command_list* c,uint32_t n,uint32_t i,uint32_t,int32_t,uint32_t){return draw(c,n,i,0,0);}
bool indirect(api::command_list* c,api::indirect_command type,api::resource,uint64_t,uint32_t n,uint32_t) {
    if(type==api::indirect_command::draw || type==api::indirect_command::draw_indexed)return draw(c,n,0,0,0);
    return false;
}
void begin(api::effect_runtime* runtime,api::command_list* cmd,api::resource_view,api::resource_view) {
    auto* context=immediate(cmd);if(!context)return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock){++feed->busy;return;}
    try {
        auto& d=feed->devices[runtime->get_device()];d.effects=true;d.bound=0;
        for(auto& [_,n]:d.draws)n=0;
        runtime->get_screenshot_width_and_height(&d.width,&d.height);
        if(d.readback)if(auto sample=d.readback->poll(context)) {
            std::unique_lock worker(feed->worker_mutex,std::try_to_lock);
            if(worker){if(feed->mailbox)++feed->replaced;feed->mailbox=std::move(sample);feed->wake.notify_one();}
            else ++feed->busy;
        }
        feed->recount();
    }catch(...){++feed->invalid;}
}
void end(api::effect_runtime* runtime,api::command_list*,api::resource_view,api::resource_view) {
    if(!feed)return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock){++feed->busy;return;}
    auto& d=feed->devices[runtime->get_device()];d.effects=false;d.bound=0;
}
void reset(api::command_list* cmd) {
    if(!immediate(cmd))return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(lock)feed->devices[cmd->get_device()].bound=0;
}
void secondary(api::command_list* cmd,api::command_list*){reset(cmd);}
void destroy_resource(api::device* device,api::resource resource) {
    if(!feed)return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock)return;
    const auto found=feed->devices.find(device);if(found==feed->devices.end())return;
    found->second.draws.erase(resource.handle);if(found->second.bound==resource.handle)found->second.bound=0;
}
void destroy_device(api::device* device) {
    if(!feed)return;
    std::unique_lock lock(feed->gpu_mutex,std::try_to_lock);if(!lock){++feed->invalid;return;}
    feed->devices.erase(device);feed->recount();
}
}
HostCameraFeed::HostCameraFeed(bool enabled,const std::filesystem::path& path,Receiver& receiver):impl_(std::make_unique<Impl>(enabled,path,receiver)){}
HostCameraFeed::~HostCameraFeed(){uninstall();}
void HostCameraFeed::install() {
    if(!impl_->enabled || impl_->installed)return;
    feed=impl_.get();impl_->ending=false;impl_->worker=std::thread(&Impl::loop,impl_.get());impl_->installed=true;
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind);
    reshade::register_event<reshade::addon_event::draw>(draw);
    reshade::register_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::register_event<reshade::addon_event::reset_command_list>(reset);
    reshade::register_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::register_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::register_event<reshade::addon_event::destroy_device>(destroy_device);
}
void HostCameraFeed::uninstall() {
    if(!impl_->installed)return;
    reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(bind);
    reshade::unregister_event<reshade::addon_event::draw>(draw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(indexed);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(indirect);
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(begin);
    reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(end);
    reshade::unregister_event<reshade::addon_event::reset_command_list>(reset);
    reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(secondary);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(destroy_resource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(destroy_device);
    feed=nullptr;impl_->installed=false;impl_->devices.clear();impl_->pending=0;
    {std::lock_guard lock(impl_->worker_mutex);impl_->ending=true;impl_->mailbox.reset();}
    impl_->wake.notify_all();if(impl_->worker.joinable())impl_->worker.join();
}
nlohmann::json HostCameraFeed::report() const {
    if(!impl_->enabled)return {{"enabled",false}};
    std::lock_guard lock(impl_->worker_mutex);
    return {{"enabled",true},{"queued",impl_->queued.load()},{"decoded",impl_->decoded.load()},{"invalid",impl_->invalid.load()},
        {"busy",impl_->busy.load()},{"replaced",impl_->replaced.load()},{"pending",impl_->pending.load()},
        {"lastCamera",impl_->last_camera},{"lastError",impl_->error},{"depthPaired",false}};
}
}
