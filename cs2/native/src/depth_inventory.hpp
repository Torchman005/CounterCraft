#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cc {
enum class DepthKind : uint8_t { unknown, d16, d24s8, d32, d32s8 };
// Values mirror reshade::api::resource_view_type, but keep the core library
// independent from the ReShade headers so view normalization is testable.
enum class DepthViewType : uint32_t {
    unknown = 0,
    buffer = 1,
    texture_1d = 2,
    texture_1d_array = 3,
    texture_2d = 4,
    texture_2d_array = 5,
    texture_2d_multisample = 6,
    texture_2d_multisample_array = 7,
    texture_3d = 8,
    texture_cube = 9,
    texture_cube_array = 10,
};
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
struct DepthSubresources {
    uint32_t first_level{}, levels{}, first_layer{}, layers{};
    bool operator==(const DepthSubresources&) const = default;
};
struct DepthView {
    DepthViewType type{};
    uint32_t format{};
    DepthSubresources raw{}, normalized{};
    bool format_compatible{}, canonical{};
    bool operator==(const DepthView&) const = default;
};
// Resolve API default ranges (UINT32_MAX) and ignored fields for D3D11 2D
// depth views, then classify the view against the resource's canonical DSV.
DepthView normalize_depth_view(DepthViewType type, uint32_t format,
    uint32_t first_level, uint32_t levels, uint32_t first_layer, uint32_t layers,
    const DepthDescription& resource, bool format_compatible);
struct DepthViewCounts {
    DepthView description{};
    uint64_t binds{}, draws{}, elements{}, indirect{}, clears{};
    double last_clear{};
    bool has_clear{};
};
struct DepthCounts {
    uint64_t draws{}, elements{}, indirect{}, clears{}, non_base_view_draws{};
    double last_clear{};
    bool has_clear{};
    std::array<DepthViewCounts,4> views{};
    uint32_t view_count{};
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
// Four distinct view descriptions per resource/interval share draw/clear counts;
// overflow preserves aggregate counts but cannot attribute the extra views.
class DepthInventory {
public:
    static constexpr size_t resource_limit=128, binding_limit=64, device_limit=8;
    void observe(DepthKey, const DepthDescription&, bool new_lifetime=false);
    void destroy_resource(DepthKey);
    void bind(uint64_t device, uint64_t command, uint64_t resource, const DepthDescription&, bool base_view=true);
    void bind_view(uint64_t device, uint64_t command, uint64_t resource, const DepthDescription&, const DepthView&);
    void unbind(uint64_t device, uint64_t command);
    void draw(uint64_t device, uint64_t command, uint32_t elements, uint32_t instances, bool indirect=false);
    void clear(DepthKey, const DepthDescription&, double value, const DepthView* view=nullptr);
    void begin_effects(uint64_t device, uint32_t width, uint32_t height);
    void end_effects(uint64_t device);
    void destroy_device(uint64_t device);
    std::array<DepthReport,device_limit> reports() const;
    uint64_t overflow() const { return overflow_; }
    size_t resource_count() const;
    size_t binding_count() const;
private:
    struct Resource { DepthKey key; uint64_t id{}; DepthDescription desc; DepthCounts current, last; };
    struct Binding { uint64_t device{}, command{}, resource_id{}; bool base_view{}; uint32_t view_index=UINT32_MAX; };
    struct Device { uint64_t key{}, id{}, interval{}; uint32_t width{}, height{}; bool effects{}; };
    std::array<Resource,resource_limit> resources_{};
    std::array<Binding,binding_limit> bindings_{};
    std::array<Device,device_limit> devices_{};
    uint64_t next_resource_{}, next_device_{}, overflow_{};
    Device* device(uint64_t key);
    Resource* resource(DepthKey);
    Resource* by_id(uint64_t id);
    uint32_t view_slot(DepthCounts&, const DepthView&);
    void bind_impl(uint64_t device, uint64_t command, uint64_t resource, const DepthDescription&, bool base_view, const DepthView*);
};
}
