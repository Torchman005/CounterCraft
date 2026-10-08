// Offline ReShade candidate. Optional capture reads public D3D11 bindings only.
#include "receiver.hpp"
#include "offline_guard.hpp"
#include "host_probe.hpp"
#include "host_capture.hpp"
#include "host_camera_feed.hpp"
#include <reshade.hpp>
#include <shellapi.h>
#include <json.hpp>
#include <condition_variable>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include "mouse_input.hpp"

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
    cc::HostCapture host_capture;
    cc::HostCameraFeed camera_feed;
    bool preview{},gameplay{};
    cc::MouseMotion sampled_mouse;
    std::atomic<uint64_t> sampled_events{};
    std::atomic<uint32_t> sampled_x{},sampled_y{};
    bool input_enabled{true};
    int64_t scroll_total{};
    double yaw{},pitch{},cursor_x{.5},cursor_y{.5}; int selected_slot{};
    bool view_seeded{};
    std::atomic<uint64_t> input_frames{};
    std::atomic<uint64_t> attack_frames{},use_frames{};
    std::mutex runtimes_mutex, report_mutex;
    std::unordered_map<api::effect_runtime*,RuntimeTextures> runtimes;
    std::atomic<uint64_t> uploads{}, failures{}, runtime_count{};
    std::atomic<uint32_t> width{}, height{}, api_id{};
    std::condition_variable report_wake;
    bool ending{};
    std::thread reporter;

    AddonState(bool p, bool probe, bool capture, bool camera, bool play, const std::filesystem::path& path)
        : receiver(37122,20,play), host_probe(probe), host_capture(capture,path), camera_feed(camera && !play,path,receiver), preview(p),gameplay(play) {}
    void log_report(bool final) {
        const auto status=receiver.stats();
        const auto start=std::chrono::steady_clock::now();
        auto report=nlohmann::json{{"countercraftProbe",true},{"preview",preview},{"finalReport",final},
            {"api",api_id.load()},{"runtimes",runtime_count.load()},
            {"size",{width.load(),height.load()}},{"uploads",uploads.load()},
            {"resourceFailures",failures.load()},{"received",status.received},
            {"connected",status.connected},{"failure",status.failure},
            {"hostCameraDepthVerified",false},{"hostDepthProbe",host_probe.report()},
            {"hostDepthCapture",host_capture.report()},{"hostCameraFeed",camera_feed.report()},
            {"camerasSent",status.cameras_sent},{"cameraReleases",status.camera_releases},
            {"cameraFramesMatched",status.cameras_rendered}};
        report["gameplay"]=gameplay;report["inputsSent"]=status.inputs_sent;report["reconnects"]=status.reconnects;
        report["sampledMouseChanges"]=sampled_events.load();report["inputFrames"]=input_frames.load();
        report["sampledCursor"]={sampled_x.load(),sampled_y.load()};
        report["attackInputFrames"]=attack_frames.load();report["useInputFrames"]=use_frames.load();
        report["guestEye"]=status.player_eye;report["guestRotation"]=status.player_rotation;report["guestGuiOpen"]=status.gui_open;
        if(gameplay)report["guestPlayer"]=status.player_status;
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
            const auto poll=std::chrono::milliseconds(host_probe.enabled()?2:gameplay?10:1000);
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
        } catch (...) { /* Never propagate into the game. */ }
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

void input_frame(api::effect_runtime* runtime) {
    if(!state->gameplay)return;
    auto window=static_cast<HWND>(runtime->get_hwnd());
    if(GetForegroundWindow()!=window || !state->receiver.stats().connected) {state->sampled_mouse.reset();state->view_seeded=false;return;}
    if(runtime->is_key_pressed(VK_F8)) {state->input_enabled=!state->input_enabled;state->sampled_mouse.reset();state->view_seeded=false;}
    if(!state->input_enabled)return;
    const auto frame=state->receiver.latest();
    if(!frame)return;
    if(!state->view_seeded){state->yaw=frame->metadata.rotation[0];state->pitch=frame->metadata.rotation[1];state->view_seeded=true;}
    const bool gui=frame && frame->metadata.gui_open;
    int dx=0,dy=0;
    uint32_t px=0,py=0;int16_t wheel=0;runtime->get_mouse_cursor_position(&px,&py,&wheel);
    RECT rect{};GetClientRect(window,&rect);POINT center{rect.right/2,rect.bottom/2};ClientToScreen(window,&center);
    // ReShade starts with an uninitialized cached (0,0). Seed the first real
    // position instead of treating focus acquisition as a huge mouse movement.
    const auto sample=(px || py)?state->sampled_mouse.update(int(px),int(py),center.x,center.y):std::pair<int,int>{0,0};
    state->sampled_x=px;state->sampled_y=py;
    dx=sample.first;dy=sample.second;
    if(sample.first || sample.second)++state->sampled_events;
    if(!gui) {state->yaw=std::remainder(state->yaw+dx*.12,360.0);state->pitch=std::clamp(state->pitch+dy*.12,-90.0,90.0);}
    cc::Receiver::Input input;
    input.yaw=state->yaw;input.pitch=state->pitch;input.captured_ns=cc::monotonic_ns();
    input.forward=double(runtime->is_key_down('W'))-double(runtime->is_key_down('S'));
    input.sideways=double(runtime->is_key_down('A'))-double(runtime->is_key_down('D'));
    input.jump=runtime->is_key_down(VK_SPACE);input.sneak=runtime->is_key_down(VK_SHIFT);input.sprint=runtime->is_key_down(VK_CONTROL);
    // Some host input modes do not populate ReShade's right-button cache.
    // Read held physical state only after the exact foreground-window guard.
    input.attack=runtime->is_mouse_button_down(0) || (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    input.use=runtime->is_key_down('R') || runtime->is_mouse_button_down(2) || (GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    if(input.attack)++state->attack_frames;if(input.use)++state->use_frames;
    input.inventory=runtime->is_key_down('E');input.escape=runtime->is_key_down(VK_ESCAPE);
    input.drop=runtime->is_key_down('Q');input.swap=runtime->is_key_down('F');
    input.pick=runtime->is_mouse_button_down(1) || (GetAsyncKeyState(VK_MBUTTON)&0x8000)!=0;
    const int steps=std::abs(wheel)>=120?wheel/120:wheel;
    if(steps) {state->scroll_total=std::clamp<int64_t>(state->scroll_total+steps,-1'000'000,1'000'000);
        if(!gui)state->selected_slot=((state->selected_slot-steps)%9+9)%9;}
    input.scroll=state->scroll_total;
    for(int n=0;n<9;++n)if(runtime->is_key_pressed('1'+n))state->selected_slot=n;
    input.slot=state->selected_slot;
    if(gui && rect.right>0 && rect.bottom>0) {
        state->cursor_x=std::clamp(state->cursor_x+double(dx)/rect.right,0.,1.);
        state->cursor_y=std::clamp(state->cursor_y+double(dy)/rect.bottom,0.,1.);
    }
    input.mouse_x=state->cursor_x;input.mouse_y=state->cursor_y;
    state->receiver.submit_input(input);
    ++state->input_frames;
    const auto pointer=runtime->find_uniform_variable(effect,"CCCursor");
    const auto showing=runtime->find_uniform_variable(effect,"CCGui");
    if(pointer.handle)runtime->set_uniform_value_float(pointer,float(state->cursor_x),float(state->cursor_y));
    if(showing.handle)runtime->set_uniform_value_bool(showing,gui);
    runtime->block_input_next_frame();
}

void active(api::effect_runtime* runtime, bool enabled, uint32_t w=0, uint32_t h=0) {
    const auto toggle = runtime->find_uniform_variable(effect,"CCActive");
    if (toggle.handle) runtime->set_uniform_value_bool(toggle,enabled);
    const auto size = runtime->find_uniform_variable(effect,"CCSize");
    if (size.handle) runtime->set_uniform_value_float(size,float(w),float(h));
    const auto full=runtime->find_uniform_variable(effect,"CCFullClient");
    if(full.handle)runtime->set_uniform_value_bool(full,state->gameplay);
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
        input_frame(runtime);
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
        active(runtime,state->preview && (!state->gameplay || state->input_enabled),w,h);
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
        wchar_t module_path[32768]{};
        if(!GetModuleFileNameW(addon,module_path,DWORD(std::size(module_path)))) throw std::runtime_error("Addon path unavailable");
        const bool capture=cc::has_argument(arguments,L"-countercraft-depth-capture");
        state = new AddonState(cc::has_argument(arguments,L"-countercraft-preview"),
            capture || cc::has_argument(arguments,L"-countercraft-host-probe"),capture,
            cc::has_argument(arguments,L"-countercraft-camera-relay"),
            cc::has_argument(arguments,L"-countercraft-gameplay"),
            std::filesystem::path(module_path).parent_path());
        reshade::register_event<reshade::addon_event::init_effect_runtime>(init_runtime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(destroy_runtime);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(reload_effects);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(begin_effects);
        state->host_probe.install();
        state->host_capture.install();
        state->camera_feed.install();
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
    state->host_capture.uninstall();
    state->camera_feed.uninstall();
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
