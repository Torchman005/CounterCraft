#include "receiver.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <json.hpp>
#include <array>
#include <chrono>
#include <functional>
#include <cmath>
#include <stdexcept>
#include <deque>

namespace cc {
namespace {
using json = nlohmann::json;
class Winsock {
public:
    Winsock() { WSADATA data{}; if (WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup failed"); }
    ~Winsock() { WSACleanup(); }
};
class Socket {
    SOCKET handle_ = INVALID_SOCKET;
public:
    Socket(uint16_t port, const std::atomic<bool>& stop) {
        handle_ = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if (handle_ == INVALID_SOCKET) throw std::runtime_error("Socket create failed");
        try {
            u_long nonblocking = 1;
            if (ioctlsocket(handle_,FIONBIO,&nonblocking)) throw std::runtime_error("Nonblocking socket failed");
            sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int result = connect(handle_,reinterpret_cast<sockaddr*>(&address),sizeof(address));
            if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("Loopback connection failed");
            wait(false, monotonic_ns() + 2'000'000'000, stop);
            int error = 0, size = sizeof(error);
            if (getsockopt(handle_,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size) || error)
                throw std::runtime_error("Loopback connection refused");
            BOOL yes = TRUE; setsockopt(handle_,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&yes),sizeof(yes));
        } catch (...) { closesocket(handle_); handle_ = INVALID_SOCKET; throw; }
    }
    ~Socket() { if (handle_ != INVALID_SOCKET) closesocket(handle_); }
    void wait(bool reading, int64_t deadline, const std::atomic<bool>& stop,
              const std::function<void()>& keepalive = {}) {
        while (!stop.load()) {
            if (monotonic_ns() >= deadline) throw std::runtime_error("Socket deadline exceeded");
            if (keepalive) keepalive();
            fd_set wanted, errors; FD_ZERO(&wanted); FD_ZERO(&errors); FD_SET(handle_,&wanted); FD_SET(handle_,&errors);
            timeval timeout{0,20'000};
            int result = select(0, reading ? &wanted : nullptr, reading ? nullptr : &wanted, &errors, &timeout);
            if (result == SOCKET_ERROR || FD_ISSET(handle_,&errors)) throw std::runtime_error("Socket select failed");
            if (FD_ISSET(handle_,&wanted)) return;
        }
        throw std::runtime_error("Receiver stopped");
    }
    void receive(std::span<uint8_t> target, int64_t deadline, const std::atomic<bool>& stop,
                 const std::function<void()>& keepalive = {}) {
        while (!target.empty()) {
            if (keepalive) keepalive();
            if (stop.load() || monotonic_ns() >= deadline) throw std::runtime_error("Receive stopped or timed out");
            int count = recv(handle_,reinterpret_cast<char*>(target.data()),int(target.size()),0);
            if (!count) throw std::runtime_error("Stream disconnected/partial packet");
            if (count > 0) { target = target.subspan(size_t(count)); continue; }
            if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("Socket read failed");
            wait(true,deadline,stop,keepalive);
        }
    }
    json request(json message, const std::atomic<bool>& stop) {
        message["v"] = 1;
        std::string body = message.dump() + "\n";
        int64_t deadline = monotonic_ns() + 1'000'000'000;
        std::span<const char> remaining(body);
        while (!remaining.empty()) {
            if (stop.load() || monotonic_ns() >= deadline) throw std::runtime_error("Control write timeout");
            int count = send(handle_,remaining.data(),int(remaining.size()),0);
            if (count > 0) { remaining = remaining.subspan(size_t(count)); continue; }
            if (count == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) { wait(false,deadline,stop); continue; }
            throw std::runtime_error("Control write failed");
        }
        std::string line;
        while (line.size() < 65536) {
            std::array<uint8_t,1> next{}; receive(next,deadline,stop);
            if (next[0] == '\n') {
                json reply = json::parse(line);
                if (reply.at("v") != 1) throw std::runtime_error("Minecraft control version mismatch");
                if (reply.at("type") == "error") {
                    auto reason=reply.value("message",std::string("unspecified"));
                    for(auto& c:reason) if(static_cast<unsigned char>(c)<32)c=' ';
                    throw std::runtime_error("Minecraft rejected control request: "+reason.substr(0,160));
                }
                return reply;
            }
            line += char(next[0]);
        }
        throw std::runtime_error("Control response exceeds limit");
    }
};
}

Receiver::Receiver(uint16_t port, unsigned fps, bool gameplay) : port_(port), fps_(fps), gameplay_(gameplay) {
    if (!port || fps < 1 || fps > 30) throw std::runtime_error("Invalid receiver port/FPS");
    worker_ = std::thread(&Receiver::run,this);
}
Receiver::~Receiver() { stop(); }
void Receiver::stop() {
    stop_.store(true);
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_); latest_.reset(); stats_.connected = false;
}
std::shared_ptr<const Frame> Receiver::latest() const {
    std::lock_guard lock(mutex_);
    if (!stats_.connected || !latest_ || std::abs(age_ms(*latest_)) > 500) return {};
    last_observed_ = latest_->header.sequence;
    return latest_;
}
ReceiverStats Receiver::stats() const { std::lock_guard lock(mutex_); return stats_; }
void Receiver::submit_camera(const HostCamera& camera,uint64_t sequence,int64_t captured_ns) {
    if(!sequence || sequence>uint64_t(INT64_MAX) || captured_ns<=0 || !std::isfinite(camera.roll)
        || !std::isfinite(camera.fov) || camera.fov<=0 || camera.fov>=180
        || !std::isfinite(camera.pitch) || std::abs(camera.pitch)>90 || !std::isfinite(camera.yaw)
        || std::abs(camera.roll)>.01 || !camera.right_handed)
        throw std::runtime_error("Unsupported host pose for Minecraft camera relay");
    for(auto n:camera.position)if(!std::isfinite(n))throw std::runtime_error("Invalid host camera position");
    std::lock_guard lock(mutex_);
    if(!camera_ || sequence>camera_->sequence) camera_=CameraUpdate{camera,sequence,captured_ns};
}
double Receiver::age_ms(const Frame& frame) const {
    return (double(monotonic_ns()) - double(clock_offset_.load()) - double(frame.metadata.captured_ns)) / 1e6;
}
void Receiver::submit_input(Input value) {
    if(!gameplay_)return;
    if(!std::isfinite(value.yaw) || !std::isfinite(value.pitch) || std::abs(value.pitch)>90
        || !std::isfinite(value.forward) || std::abs(value.forward)>1 || !std::isfinite(value.sideways) || std::abs(value.sideways)>1
        || !std::isfinite(value.mouse_x) || value.mouse_x<0 || value.mouse_x>1 || !std::isfinite(value.mouse_y)
        || value.mouse_y<0 || value.mouse_y>1 || value.slot<0 || value.slot>8
        || value.scroll < -1'000'000 || value.scroll > 1'000'000 || value.captured_ns<=0)
        throw std::runtime_error("Invalid gameplay input");
    std::lock_guard lock(mutex_);input_=value;
}
void Receiver::run() {
    do {
        session();
        if(!gameplay_ || stop_)return;
        for(int i=0;i<50 && !stop_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {std::lock_guard lock(mutex_);++stats_.reconnects;input_.reset();stats_.ui_dropped+=ui_.size();ui_.clear();}
    }while(!stop_);
}
void Receiver::submit_ui(UiEvent event) {
    if(!gameplay_)return;
    if(event.text.size()>256 || event.modifiers<0 || event.modifiers>7 || event.captured_ns<=0)
        throw std::runtime_error("Invalid UI event");
    std::lock_guard lock(mutex_);
    if(!stats_.connected || ui_.size()>=64){++stats_.ui_dropped;return;}
    ui_.push_back(std::move(event));
}
void Receiver::clear_ui() {std::lock_guard lock(mutex_);stats_.ui_dropped+=ui_.size();ui_.clear();}
void Receiver::session() {
    try {
        Winsock winsock;
        Socket control(port_,stop_);
        json ready = control.request({{"type","hello"},{"role","test"}},stop_);
        if (ready.at("type") != "ready" || ready.at("stream") != true) throw std::runtime_error("No Minecraft frame stream");
        if(gameplay_ && !ready.value("input",false))throw std::runtime_error("No Minecraft gameplay input provider");
        if(gameplay_ && !ready.value("ui",false))throw std::runtime_error("Restart Minecraft with UI-event capable CounterCraft mod");
        int64_t best_rtt = INT64_MAX;
        json status;
        for (int i = 0; i < 5; ++i) {
            int64_t before = monotonic_ns(); status = control.request({{"type","ping"}},stop_); int64_t after = monotonic_ns();
            if (after - before < best_rtt) {
                best_rtt = after - before;
                clock_offset_.store(before + (after - before) / 2 - status.at("serverMonotonicNanos").get<int64_t>());
            }
        }
        if (status.at("offline") != true) throw std::runtime_error("Minecraft lab is paused or not singleplayer");
        json request_start={{"type","stream-start"},{"fps",fps_}};
        if(gameplay_)request_start["fullClient"]=true;
        json start = control.request(request_start,stop_);
        if (start.at("type") != "stream-started" || start.at("host") != "127.0.0.1"
            || !start.at("port").is_number_integer()) throw std::runtime_error("Wrong stream endpoint");
        int binary_port = start.at("port").get<int>();
        if (binary_port < 1 || binary_port > 65535) throw std::runtime_error("Invalid stream port");
        Session session = Session::parse(start.at("session").get<std::string>());
        const int64_t epoch = start.at("epoch").get<int64_t>();
        Socket binary(uint16_t(binary_port),stop_);
        { std::lock_guard lock(mutex_); stats_.connected = true; stats_.failure.clear(); stats_.clock_uncertainty_ns = best_rtt / 2; }
        uint64_t sequence = 0;
        int64_t next_ping = monotonic_ns();
        int64_t next_camera = monotonic_ns();
        int64_t next_input = 0; uint64_t input_id=0; bool input_active=false;
        int64_t next_ui=0;uint64_t action_id=0;
        uint64_t sent_camera=0;
        bool camera_active=false,anchored=false;
        std::array<double,3> source_anchor{},guest_anchor{};
        // A local lab alignment: first valid host eye anchors to the existing MC
        // eye. Scale is the shared protocol's explicit 32 Source units per block.
        // This does not move the MC player or supply collision/input semantics.
        const auto initial_status=status;
        struct ExpectedPose {uint64_t sequence;std::array<double,3> position;double yaw,pitch,fov;};
        std::deque<ExpectedPose> expected_poses;
        auto heartbeat = [&] {
            const auto now=monotonic_ns();
            if(gameplay_ && now>=next_ui) {
                std::optional<UiEvent> event;
                {std::lock_guard lock(mutex_);
                    while(!ui_.empty() && (now<ui_.front().captured_ns || now-ui_.front().captured_ns>=250'000'000)) {ui_.pop_front();++stats_.ui_dropped;}
                    if(!ui_.empty()){event=std::move(ui_.front());ui_.pop_front();}
                }
                if(event) {
                    auto message=json{{"type","action"},{"action","ui"},{"id",++action_id},{"epoch",epoch},{"modifiers",event->modifiers}};
                    if(event->key)message["key"]=event->key;else message["text"]=event->text;
                    const auto ack=control.request(message,stop_);
                    if(ack.at("type")!="action-ack" || ack.at("id")!=action_id || ack.at("epoch")!=epoch || ack.at("action")!="ui")
                        throw std::runtime_error("UI action acknowledgement mismatch");
                    {std::lock_guard lock(mutex_);++stats_.ui_sent;}
                }
                next_ui=monotonic_ns()+25'000'000;
            }
            if(gameplay_ && now>=next_input) {
                std::optional<Input> in;
                {std::lock_guard lock(mutex_);in=input_;}
                const bool fresh=in && now>=in->captured_ns && now-in->captured_ns<250'000'000;
                if(fresh) {
                    const auto& v=*in;
                    const auto ack=control.request({{"type","input"},{"id",++input_id},{"epoch",epoch},
                        {"yaw",v.yaw},{"pitch",v.pitch},{"forward",v.forward},{"sideways",v.sideways},{"slot",v.slot},
                        {"jump",v.jump},{"sneak",v.sneak},{"sprint",v.sprint},{"attack",v.attack},{"use",v.use},
                        {"inventory",v.inventory},{"escape",v.escape},{"mouseX",v.mouse_x},{"mouseY",v.mouse_y},
                        {"drop",v.drop},{"swap",v.swap},{"pick",v.pick},{"scroll",v.scroll}},stop_);
                    if(ack.at("type")!="input-ack" || ack.at("id")!=input_id || ack.at("epoch")!=epoch)
                        throw std::runtime_error("Gameplay input acknowledgement mismatch");
                    input_active=true;{std::lock_guard lock(mutex_);++stats_.inputs_sent;}
                }else if(input_active) {
                    control.request({{"type","release"}},stop_);input_active=false;
                }
                next_input=monotonic_ns()+33'333'333;
            }
            if(!gameplay_ && now>=next_camera) {
                std::optional<CameraUpdate> update;
                {std::lock_guard lock(mutex_);update=camera_;}
                const bool fresh=update && now>=update->captured_ns && now-update->captured_ns<250'000'000;
                if(fresh && update->sequence>sent_camera) {
                    if(!anchored) {
                        guest_anchor=initial_status.at("position").get<std::array<double,3>>();
                        for(auto n:guest_anchor) if(!std::isfinite(n)) throw std::runtime_error("Invalid MC camera anchor");
                        source_anchor=update->camera.position;anchored=true;
                    }
                    const auto& c=update->camera;
                    const std::array<double,3> position{guest_anchor[0]+(c.position[0]-source_anchor[0])/32,
                        guest_anchor[1]+(c.position[2]-source_anchor[2])/32,guest_anchor[2]-(c.position[1]-source_anchor[1])/32};
                    const double yaw=std::remainder(-90-c.yaw,360.0);
                    const auto ack=control.request({{"type","camera"},{"frame",update->sequence},
                        {"position",position},{"rotation",{yaw,c.pitch,0}},{"fov",c.fov}},stop_);
                    if(ack.at("type")!="ack" || ack.at("frame")!=update->sequence) throw std::runtime_error("Camera acknowledgement mismatch");
                    sent_camera=update->sequence;camera_active=true;
                    expected_poses.push_back({sent_camera,position,yaw,c.pitch,c.fov});
                    if(expected_poses.size()>128)expected_poses.pop_front();
                    {std::lock_guard lock(mutex_);++stats_.cameras_sent;}
                } else if(!fresh && camera_active) {
                    if(control.request({{"type","release"}},stop_).at("type")!="released") throw std::runtime_error("Camera release not acknowledged");
                    camera_active=false;
                    {std::lock_guard lock(mutex_);++stats_.camera_releases;}
                }
                next_camera=monotonic_ns()+33'000'000;
            }
            if (monotonic_ns() >= next_ping) {
                json heartbeat = control.request({{"type","ping"}},stop_);
                if (heartbeat.at("offline") != true || heartbeat.at("epoch") != epoch
                    || heartbeat.at("stream").at("running") != true) throw std::runtime_error("World/stream unavailable");
                if(gameplay_) {std::lock_guard lock(mutex_);stats_.player_status=heartbeat.value("player",json::object());}
                next_ping = monotonic_ns() + 250'000'000;
            }
        };
        while (!stop_.load()) {
            heartbeat();
            std::array<uint8_t,64> header{};
            // Frame arrival must not delay the control heartbeat beyond its two-second lifetime.
            int64_t deadline = monotonic_ns() + 2'000'000'000;
            binary.receive(header,deadline,stop_,heartbeat);
            Header h = decode_header(header,session,sequence);
            std::vector<uint8_t> metadata(h.metadata_bytes);
            binary.receive(metadata,deadline,stop_,heartbeat);
            auto frame = std::make_shared<Frame>(); frame->header = h;
            frame->metadata = decode_metadata(std::string(metadata.begin(),metadata.end()),h,epoch);
            if(frame->metadata.full_client!=gameplay_)throw std::runtime_error("Unexpected Minecraft client layer mode");
            frame->pixels.resize(size_t(h.color_bytes) + h.depth_bytes);
            binary.receive(frame->pixels,deadline,stop_,heartbeat);
            if (crc32(frame->pixels,crc32(metadata)) != h.crc) throw std::runtime_error("Frame CRC mismatch");
            validate_depth(frame->depth()); // On the worker; never scans on the host render thread.
            bool rendered_camera=false;
            if(frame->metadata.requested_frame>=0) {
                const auto requested=uint64_t(frame->metadata.requested_frame);
                for(const auto& expected:expected_poses) if(expected.sequence==requested) {
                    const auto& actual=frame->metadata;
                    for(size_t i=0;i<3;++i) if(std::abs(actual.position[i]-expected.position[i])>.001)
                        throw std::runtime_error("Rendered MC position differs from relayed camera");
                    if(std::abs(std::remainder(actual.rotation[0]-expected.yaw,360.))>.001
                        || std::abs(actual.rotation[1]-expected.pitch)>.001 || std::abs(actual.rotation[2])>.001
                        || std::abs(actual.fov-expected.fov)>.001)
                        throw std::runtime_error("Rendered MC lens/orientation differs from relayed camera");
                    rendered_camera=true;break;
                }
            }
            frame->received_ns = monotonic_ns(); sequence = h.sequence;
            std::lock_guard lock(mutex_); ++stats_.received;
            if(gameplay_) {stats_.player_eye=frame->metadata.position;stats_.player_rotation=frame->metadata.rotation;stats_.gui_open=frame->metadata.gui_open;}
            if(rendered_camera)++stats_.cameras_rendered;
            if (std::abs(age_ms(*frame)) > 500) { ++stats_.stale; continue; }
            if (latest_ && latest_->header.sequence > last_observed_) ++stats_.replaced;
            latest_ = std::move(frame);
        }
        // Closing the host connection is the server's authoritative stop/release path.
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_); if (!stop_.load()) stats_.failure = error.what();
    }
    std::lock_guard lock(mutex_); stats_.connected = false; latest_.reset();
}
}
