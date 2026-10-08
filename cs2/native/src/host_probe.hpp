#pragma once
#include "depth_observer.hpp"
#include <json.hpp>

namespace cc {
class HostProbe {
public:
    explicit HostProbe(bool enabled) : enabled_(enabled) {}
    ~HostProbe();
    void install();
    void uninstall();
    nlohmann::json report() const;
    bool enabled() const { return enabled_; }
    void drain() noexcept { if(enabled_) observer_.drain(); }
    void deferred() noexcept { observer_.deferred(); }
    void observe(const DepthEvent& event) noexcept { if(enabled_) observer_.submit(event); }
    void failed() noexcept { observer_.failed(); }
private:
    bool enabled_{}, installed_{};
    DepthObserver observer_;
};
}
