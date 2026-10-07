#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cc {
enum class DepthKind : uint8_t { unknown, d16, d24s8, d32, d32s8 };
struct DepthKey {
    uint64_t device{}, resource{}; // Non-owning identity, never dereferenced.
    bool operator==(const DepthKey&) const = default;
};
struct DepthDescription {
    uint32_t width{}, height{}, layers{}, levels{}, samples{}, format{};
    DepthKind kind{};
    bool shader_readable{};
    bool operator==(const DepthDescription&) const = default;
    bool sampling_shape() const;
};
struct DepthCounts {
    uint64_t draws{}, elements{}, indirect{}, clears{}, non_base_view_draws{};
    double last_clear{};
    bool has_clear{};
};
struct DepthCandidate {
    uint64_t id{};
    DepthDescription description;
    DepthCounts counts;
    bool matches_output{};
};
struct DepthReport {
    uint64_t device_id{}, interval{}, overflow{};
    uint32_t width{}, height{};
    std::array<DepthCandidate,8> candidates{};
    uint32_t count{};
};
// Fixed-size metadata only. Callers serialize access; callbacks never allocate here.
// These are candidate observations, not a scene-depth selector or GPU resource owner.
class DepthInventory {
public:
    static constexpr size_t resource_limit=128, binding_limit=64, device_limit=8;
    void observe(DepthKey, const DepthDescription&, bool new_lifetime=false);
    void destroy_resource(DepthKey);
    void bind(uint64_t device, uint64_t command, uint64_t resource, const DepthDescription&, bool base_view=true);
    void unbind(uint64_t device, uint64_t command);
    void draw(uint64_t device, uint64_t command, uint32_t elements, uint32_t instances, bool indirect=false);
    void clear(DepthKey, const DepthDescription&, double value);
    void begin_effects(uint64_t device, uint32_t width, uint32_t height);
    void end_effects(uint64_t device);
    void destroy_device(uint64_t device);
    std::array<DepthReport,device_limit> reports() const;
    uint64_t overflow() const { return overflow_; }
    size_t resource_count() const;
    size_t binding_count() const;
private:
    struct Resource { DepthKey key; uint64_t id{}; DepthDescription desc; DepthCounts current, last; };
    struct Binding { uint64_t device{}, command{}, resource_id{}; bool base_view{}; };
    struct Device { uint64_t key{}, id{}, interval{}; uint32_t width{}, height{}; bool effects{}; };
    std::array<Resource,resource_limit> resources_{};
    std::array<Binding,binding_limit> bindings_{};
    std::array<Device,device_limit> devices_{};
    uint64_t next_resource_{}, next_device_{}, overflow_{};
    Device* device(uint64_t key);
    Resource* resource(DepthKey);
    Resource* by_id(uint64_t id);
};
}
