#pragma once
#include "bounded_event_queue.hpp"
#include "depth_inventory.hpp"

namespace cc {
enum class DepthEventKind : uint8_t {
    observe, destroy_resource, destroy_device, bind, unbind, draw, clear,
    begin_effects, end_effects, count
};
// Value metadata only: no native pointers, references, callbacks or COM owners.
struct DepthEvent {
    DepthEventKind kind{};
    uint64_t device{}, command{}, resource{};
    DepthDescription description{};
    DepthView view{};
    uint32_t elements{}, instances{}, width{}, height{};
    double clear_value{};
    bool new_lifetime{}, indirect{}, has_view{};
};
struct DepthObserverSnapshot {
    std::array<DepthReport,DepthInventory::device_limit> devices{};
    uint64_t queued{}, processed{}, pending{}, peak_pending_at_drain{};
    uint64_t full_events{}, contended_events{}, callback_failures{}, deferred_events{}, overflow{};
    std::array<uint64_t,size_t(DepthEventKind::count)> rejected_by_kind{};
    uint64_t drain_batches{}, max_batch_events{}, total_drain_us{}, max_drain_us{}, snapshot_build_us{};
    uint64_t missed() const { return full_events+contended_events+callback_failures; }
    bool known_loss_free() const { return missed()==0 && deferred_events==0 && overflow==0; }
};
// submit/failed/deferred are callback-safe. drain/snapshot have exactly one
// background consumer, which exclusively owns the inventory and report work.
class DepthObserver {
public:
    static constexpr size_t queue_capacity=16384;
    bool submit(const DepthEvent&) noexcept;
    void failed() noexcept { callback_failures_.fetch_add(1,std::memory_order_relaxed); }
    void deferred() noexcept { deferred_events_.fetch_add(1,std::memory_order_relaxed); }
    size_t drain(size_t budget=queue_capacity) noexcept;
    DepthObserverSnapshot snapshot() const;
private:
    BoundedEventQueue<DepthEvent,queue_capacity> queue_;
    DepthInventory inventory_;
    std::atomic<uint64_t> full_events_{}, contended_events_{}, callback_failures_{}, deferred_events_{};
    std::array<std::atomic<uint64_t>,size_t(DepthEventKind::count)> rejected_by_kind_{};
    uint64_t peak_pending_at_drain_{}, drain_batches_{}, max_batch_events_{}, total_drain_us_{}, max_drain_us_{};
    void apply(const DepthEvent&) noexcept;
};
}
