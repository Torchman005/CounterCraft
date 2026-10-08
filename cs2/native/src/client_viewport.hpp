#pragma once
#include <algorithm>
#include <cmath>

namespace cc {
// Same centered aspect fit as CounterCraftProbe.fx, in client pixels.
struct ClientViewport { double width{}, height{}; };
inline ClientViewport fit_client(double host_width, double host_height, double guest_width, double guest_height) {
    if (!std::isfinite(host_width) || !std::isfinite(host_height) || !std::isfinite(guest_width)
        || !std::isfinite(guest_height) || host_width <= 0 || host_height <= 0 || guest_width <= 0 || guest_height <= 0)
        return {};
    const double scale = std::min(host_width / guest_width, host_height / guest_height);
    return {guest_width * scale, guest_height * scale};
}
}
