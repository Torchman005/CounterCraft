#pragma once
#include "depth_inventory.hpp"
#include <json.hpp>
#include <atomic>
#include <mutex>

namespace cc {
class HostProbe {
public:
    explicit HostProbe(bool enabled) : enabled_(enabled) {}
    ~HostProbe();
    void install();
    void uninstall();
    nlohmann::json report() const;
    void deferred() { ++deferred_events_; }
    template<class F> void observe(F&& operation) noexcept {
        if (!enabled_) return;
        try {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()) { ++missed_events_; return; }
            operation(inventory_);
        } catch (...) { ++missed_events_; }
    }
    void failed() { ++missed_events_; }
private:
    bool enabled_{}, installed_{};
    mutable std::mutex mutex_;
    DepthInventory inventory_;
    std::atomic<uint64_t> missed_events_{}, deferred_events_{};
};
}
