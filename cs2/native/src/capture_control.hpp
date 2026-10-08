#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <json.hpp>

namespace cc {
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
}
