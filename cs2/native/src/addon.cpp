// Diagnostic ReShade candidate. It does not read CS2 memory, camera or scene depth.
#include "receiver.hpp"
#include "offline_guard.hpp"
#include "host_probe.hpp"
#include <reshade.hpp>
#include <shellapi.h>
#include <json.hpp>
#include <condition_variable>
#include <memory>
#include <unordered_map>

namespace {
namespace api = reshade::api;
constexpr const char* effect = "CounterCraftProbe.fx";

struct RuntimeTextures {
    api::device* device{};
    api::resource color{}, depth{};
    api::resource_view color_view{}, depth_view{};
    uint32_t width{}, height{};
    uint64_t sequence{};
    bool failed{};

    void release(api::effect_runtime* runtime) {
        runtime->update_texture_bindings("COUNTERCRAFT_COLOR",{},{});
        runtime->update_texture_bindings("COUNTERCRAFT_DEPTH",{},{});
        if (color_view.handle) device->destroy_resource_view(color_view);
        if (depth_view.handle) device->destroy_resource_view(depth_view);
        if (color.handle) device->destroy_resource(color);
        if (depth.handle) device->destroy_resource(depth);
        color = {}; depth = {}; color_view = {}; depth_view = {};
        width = height = 0; sequence = 0;
    }

    bool create(uint32_t w, uint32_t h) {
        const auto usage = api::resource_usage::shader_resource | api::resource_usage::copy_dest;
        const api::resource_desc color_desc(w,h,1,1,api::format::r8g8b8a8_unorm,1,api::memory_heap::default_,usage);
        const api::resource_desc depth_desc(w,h,1,1,api::format::r32_float,1,api::memory_heap::default_,usage);
        if (!device->create_resource(color_desc,nullptr,api::resource_usage::shader_resource,&color)
            || !device->create_resource(depth_desc,nullptr,api::resource_usage::shader_resource,&depth)
            || !device->create_resource_view(color,api::resource_usage::shader_resource,
                api::resource_view_desc(api::format::r8g8b8a8_unorm),&color_view)
            || !device->create_resource_view(depth,api::resource_usage::shader_resource,
                api::resource_view_desc(api::format::r32_float),&depth_view)) return false;
        width = w; height = h; return true;
    }

    void bind(api::effect_runtime* runtime) {
        runtime->update_texture_bindings("COUNTERCRAFT_COLOR",color_view,color_view);
        runtime->update_texture_bindings("COUNTERCRAFT_DEPTH",depth_view,depth_view);
    }
};

struct AddonState {
    cc::Receiver receiver;
    cc::HostProbe host_probe;
    bool preview{};
    std::mutex runtimes_mutex, report_mutex;
    std::unordered_map<api::effect_runtime*,RuntimeTextures> runtimes;
    std::atomic<uint64_t> uploads{}, failures{}, runtime_count{};
    std::atomic<uint32_t> width{}, height{}, api_id{};
    std::condition_variable report_wake;
    bool ending{};
    std::thread reporter;

    AddonState(bool p, bool probe) : host_probe(probe), preview(p) {}
    void log_report(bool final) {
        const auto status=receiver.stats();
        const auto start=std::chrono::steady_clock::now();
        auto report=nlohmann::json{{"countercraftProbe",true},{"preview",preview},{"finalReport",final},
            {"api",api_id.load()},{"runtimes",runtime_count.load()},
            {"size",{width.load(),height.load()}},{"uploads",uploads.load()},
            {"resourceFailures",failures.load()},{"received",status.received},
            {"connected",status.connected},{"failure",status.failure},
            {"hostCameraDepthVerified",false},{"hostDepthProbe",host_probe.report()}};
        // Includes snapshot and JSON tree construction, excludes dump/disk logging.
        report["reportBuildUs"]=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-start).count();
        const auto message=report.dump();
        reshade::log::message(reshade::log::level::info,message.c_str());
    }
    void report_loop() {
        try {
            std::unique_lock lock(report_mutex);
            auto next_report=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            const auto poll=std::chrono::milliseconds(host_probe.enabled()?2:1000);
            while (!report_wake.wait_for(lock,poll,[&]{ return ending; })) {
                lock.unlock();
                host_probe.drain();
                if(std::chrono::steady_clock::now()>=next_report) {
                    // All inventory work, snapshots, JSON and disk logging share
                    // this consumer. Producers cannot contend with report work.
                    log_report(false);
                    next_report=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                }
                lock.lock();
            }
            lock.unlock();
            // AddonUninit unregisters producers before stop(). Drain the bounded
            // tail and log teardown state instead of losing the last interval.
            host_probe.drain();
            log_report(true);
        } catch (...) { /* Never propagate an exception into the game. */ }
    }
    void stop() {
        { std::lock_guard lock(report_mutex); ending = true; }
        report_wake.notify_all();
        if (reporter.joinable()) reporter.join();
        receiver.stop();
    }
    ~AddonState() { stop(); }
};

// No owning global destructor: a forced process termination must not join under loader lock.
// ReShade 6.8 calls AddonUninit before FreeLibrary; normal cleanup is explicit there.
AddonState* state = nullptr;

void active(api::effect_runtime* runtime, bool enabled, uint32_t w=0, uint32_t h=0) {
    const auto toggle = runtime->find_uniform_variable(effect,"CCActive");
    if (toggle.handle) runtime->set_uniform_value_bool(toggle,enabled);
    const auto size = runtime->find_uniform_variable(effect,"CCSize");
    if (size.handle) runtime->set_uniform_value_float(size,float(w),float(h));
}

void init_runtime(api::effect_runtime* runtime) {
    if (!state) return;
    try {
        auto* device = runtime->get_device();
        state->api_id.store(uint32_t(device->get_api()));
        if (device->get_api() != api::device_api::d3d11) return;
        std::lock_guard lock(state->runtimes_mutex);
        state->runtimes.try_emplace(runtime).first->second.device = device;
        state->runtime_count.store(state->runtimes.size());
    } catch (...) { ++state->failures; }
}

void destroy_runtime(api::effect_runtime* runtime) {
    if (!state) return;
    try {
        std::lock_guard lock(state->runtimes_mutex);
        auto it = state->runtimes.find(runtime);
        if (it == state->runtimes.end()) return;
        active(runtime,false); it->second.release(runtime); state->runtimes.erase(it);
        state->runtime_count.store(state->runtimes.size());
    } catch (...) { ++state->failures; }
}

void reload_effects(api::effect_runtime* runtime) {
    if (!state) return;
    try {
        active(runtime,false);
        const auto technique = runtime->find_technique(effect,"CounterCraftProbe");
        if (technique.handle) runtime->set_technique_state(technique,state->preview);
    } catch (...) { ++state->failures; }
}

void begin_effects(api::effect_runtime* runtime, api::command_list* commands,
                   api::resource_view, api::resource_view) {
    if (!state) return;
    try {
        std::lock_guard lock(state->runtimes_mutex);
        const auto found = state->runtimes.find(runtime);
        if (found == state->runtimes.end()) { active(runtime,false); return; }
        auto& textures = found->second;
        const auto frame = state->receiver.latest();
        if (!frame || textures.failed) { active(runtime,false); return; }
        const auto w = uint32_t(frame->metadata.width), h = uint32_t(frame->metadata.height);
        if (textures.width != w || textures.height != h) {
            textures.release(runtime);
            if (!textures.create(w,h)) {
                textures.release(runtime); textures.failed = true; ++state->failures;
                active(runtime,false); return;
            }
        }
        if (textures.sequence != frame->header.sequence) {
            // D3D11-only immediate uploads; no sockets, CRC scans, clock requests or joins here.
            commands->barrier(textures.color,api::resource_usage::shader_resource,api::resource_usage::copy_dest);
            commands->barrier(textures.depth,api::resource_usage::shader_resource,api::resource_usage::copy_dest);
            const api::subresource_data color{const_cast<uint8_t*>(frame->rgba().data()),w*4,w*h*4};
            const api::subresource_data depth{const_cast<uint8_t*>(frame->depth().data()),w*4,w*h*4};
            textures.device->update_texture_region(color,textures.color,0);
            textures.device->update_texture_region(depth,textures.depth,0);
            commands->barrier(textures.color,api::resource_usage::copy_dest,api::resource_usage::shader_resource);
            commands->barrier(textures.depth,api::resource_usage::copy_dest,api::resource_usage::shader_resource);
            textures.sequence = frame->header.sequence;
            ++state->uploads; state->width.store(w); state->height.store(h);
        }
        textures.bind(runtime); // Also refresh semantic bindings after an effect reload.
        active(runtime,state->preview,w,h);
    } catch (...) {
        ++state->failures;
        try { active(runtime,false); } catch (...) {}
    }
}
}

extern "C" __declspec(dllexport) const char* NAME = "CounterCraft offline upload probe";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "Diagnostic only; CS2 camera and scene depth are not integrated.";

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon, HMODULE reshade_module) {
    if (state) return false;
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));
    if (!length || length >= std::size(executable)) return false;
    int count = 0;
    auto** parsed = CommandLineToArgvW(GetCommandLineW(),&count);
    if (!parsed) return false;
    std::vector<std::wstring> arguments;
    for (int i=1; i<count; ++i) arguments.emplace_back(parsed[i]);
    LocalFree(parsed);
    if (!cc::offline_lab_allowed(executable,arguments)) return false;
    if (!reshade::register_addon(addon,reshade_module)) return false;
    try {
        state = new AddonState(cc::has_argument(arguments,L"-countercraft-preview"),
            cc::has_argument(arguments,L"-countercraft-host-probe"));
        reshade::register_event<reshade::addon_event::init_effect_runtime>(init_runtime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(destroy_runtime);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(reload_effects);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
        state->host_probe.install();
        state->reporter = std::thread(&AddonState::report_loop,state);
        reshade::log::message(reshade::log::level::info,"CounterCraft upload candidate: offline guard passed. Host camera/depth unverified.");
        return true;
    } catch (...) {
        delete state; state = nullptr;
        reshade::unregister_addon(addon,reshade_module); return false;
    }
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon, HMODULE reshade_module) {
    if (!state) return;
    state->host_probe.uninstall();
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
    reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(reload_effects);
    reshade::unregister_event<reshade::addon_event::init_effect_runtime>(init_runtime);
    reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(destroy_runtime);
    state->stop();
    for (auto& [runtime,textures] : state->runtimes) { active(runtime,false); textures.release(runtime); }
    delete state; state = nullptr;
    reshade::unregister_addon(addon,reshade_module);
}

BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID) {
    // Keep thread notifications enabled for the static CRT.
    return TRUE; // No initialization, sockets, waits or thread joins under loader lock.
}
