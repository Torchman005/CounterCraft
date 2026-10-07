#include "receiver.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <json.hpp>
#include <array>
#include <chrono>
#include <functional>
#include <cmath>
#include <stdexcept>

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
                if (reply.at("v") != 1 || reply.at("type") == "error") throw std::runtime_error("Minecraft rejected control request");
                return reply;
            }
            line += char(next[0]);
        }
        throw std::runtime_error("Control response exceeds limit");
    }
};
}

Receiver::Receiver(uint16_t port, unsigned fps) : port_(port), fps_(fps) {
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
double Receiver::age_ms(const Frame& frame) const {
    return (double(monotonic_ns()) - double(clock_offset_.load()) - double(frame.metadata.captured_ns)) / 1e6;
}
void Receiver::run() {
    try {
        Winsock winsock;
        Socket control(port_,stop_);
        json ready = control.request({{"type","hello"},{"role","test"}},stop_);
        if (ready.at("type") != "ready" || ready.at("stream") != true) throw std::runtime_error("No Minecraft frame stream");
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
        json start = control.request({{"type","stream-start"},{"fps",fps_}},stop_);
        if (start.at("type") != "stream-started" || start.at("host") != "127.0.0.1"
            || !start.at("port").is_number_integer()) throw std::runtime_error("Wrong stream endpoint");
        int binary_port = start.at("port").get<int>();
        if (binary_port < 1 || binary_port > 65535) throw std::runtime_error("Invalid stream port");
        Session session = Session::parse(start.at("session").get<std::string>());
        const int64_t epoch = start.at("epoch").get<int64_t>();
        Socket binary(uint16_t(binary_port),stop_);
        { std::lock_guard lock(mutex_); stats_.connected = true; stats_.clock_uncertainty_ns = best_rtt / 2; }
        uint64_t sequence = 0;
        int64_t next_ping = monotonic_ns();
        auto heartbeat = [&] {
            if (monotonic_ns() >= next_ping) {
                json heartbeat = control.request({{"type","ping"}},stop_);
                if (heartbeat.at("offline") != true || heartbeat.at("epoch") != epoch
                    || heartbeat.at("stream").at("running") != true) throw std::runtime_error("World/stream unavailable");
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
            frame->pixels.resize(size_t(h.color_bytes) + h.depth_bytes);
            binary.receive(frame->pixels,deadline,stop_,heartbeat);
            if (crc32(frame->pixels,crc32(metadata)) != h.crc) throw std::runtime_error("Frame CRC mismatch");
            validate_depth(frame->depth()); // On the worker; never scans on the host render thread.
            frame->received_ns = monotonic_ns(); sequence = h.sequence;
            std::lock_guard lock(mutex_); ++stats_.received;
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
