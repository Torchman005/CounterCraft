#pragma once
#include "frame.hpp"
#include "host_camera.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>

namespace cc {
struct ReceiverStats {
    uint64_t received{}, replaced{}, stale{};
    bool connected{};
    std::string failure;
    int64_t clock_uncertainty_ns{};
    uint64_t cameras_sent{},camera_releases{};
    uint64_t cameras_rendered{};
};
class Receiver {
public:
    explicit Receiver(uint16_t port = 37122, unsigned fps = 20);
    ~Receiver();
    Receiver(const Receiver&) = delete;
    std::shared_ptr<const Frame> latest() const;
    ReceiverStats stats() const;
    void stop();
    double age_ms(const Frame&) const;
    void submit_camera(const HostCamera&,uint64_t sequence,int64_t captured_ns);
private:
    void run();
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<int64_t> clock_offset_{0};
    uint16_t port_;
    unsigned fps_;
    mutable std::mutex mutex_;
    std::shared_ptr<const Frame> latest_;
    mutable uint64_t last_observed_{};
    ReceiverStats stats_;
    struct CameraUpdate { HostCamera camera; uint64_t sequence{}; int64_t captured_ns{}; };
    std::optional<CameraUpdate> camera_;
};
}
