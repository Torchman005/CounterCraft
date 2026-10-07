#pragma once
#include "frame.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace cc {
struct ReceiverStats {
    uint64_t received{}, replaced{}, stale{};
    bool connected{};
    std::string failure;
    int64_t clock_uncertainty_ns{};
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
};
}
