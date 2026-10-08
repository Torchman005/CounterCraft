#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <json.hpp>

namespace cc {
inline bool capture_transitions(const nlohmann::json& config) {
    const auto trigger=config.value("trigger",std::string("draw-milestones"));
    if(trigger!="draw-milestones" && trigger!="viewport-transitions")
        throw std::runtime_error("Unknown capture trigger");
    return trigger=="viewport-transitions";
}
inline uint64_t capture_sequence(const nlohmann::json& config) {
    if(!config.is_object() || !config.contains("manual") || config.at("manual") != true
        || !config.contains("request") || !config.at("request").is_number_unsigned())
        throw std::runtime_error("Manual capture control requires an unsigned request");
    const auto sequence=config.at("request").get<uint64_t>();
    if(sequence>1000000) throw std::runtime_error("Capture request too large");
    return sequence;
}

// One writer observes file updates; one consumer runs under the GPU mutex.
// Startup establishes a baseline: requests from a previous process are not replayed.
struct CaptureRequests {
    std::atomic<uint64_t> requested{},consumed{};
    void initialize(uint64_t baseline) { requested.store(baseline); consumed.store(baseline); }
    void observe(uint64_t value) { if(value>requested.load()) requested.store(value); }
    std::optional<uint64_t> take(bool has_capacity) {
        const auto previous=consumed.load();
        if(!has_capacity || previous>=requested.load()) return std::nullopt;
        consumed.store(previous+1); return previous+1;
    }
};

struct ViewportCaptureSchedule {
    using Viewport=std::array<float,6>;
    std::optional<Viewport> previous;
    uint64_t transitions{};
    void reset() { previous.reset(); transitions=0; }
    // The first interval snapshot is taken at draw 64 for camera context. Then
    // capture only at viewport changes, before the first draw with the new state.
    bool observe(uint64_t draw, const Viewport& viewport) {
        const bool changed=previous && *previous!=viewport;
        if(changed) ++transitions;
        previous=viewport;
        return changed || draw==64;
    }
};
}
