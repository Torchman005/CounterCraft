#include "depth_observer.hpp"
#include <algorithm>
#include <chrono>

namespace cc {
namespace {
using Clock=std::chrono::steady_clock;
uint64_t elapsed_us(Clock::time_point start) {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
}
}
bool DepthObserver::submit(const DepthEvent& event) noexcept {
    const auto kind=size_t(event.kind);
    if(kind>=rejected_by_kind_.size()) { failed(); return false; }
    const auto result=queue_.push(event);
    if(result==QueuePush::accepted) return true;
    if(result==QueuePush::full) full_events_.fetch_add(1,std::memory_order_relaxed);
    else contended_events_.fetch_add(1,std::memory_order_relaxed);
    rejected_by_kind_[kind].fetch_add(1,std::memory_order_relaxed);
    return false;
}
void DepthObserver::apply(const DepthEvent& e) noexcept {
    switch(e.kind) {
    case DepthEventKind::observe: inventory_.observe({e.device,e.resource},e.description,e.new_lifetime); break;
    case DepthEventKind::destroy_resource: inventory_.destroy_resource({e.device,e.resource}); break;
    case DepthEventKind::destroy_device: inventory_.destroy_device(e.device); break;
    case DepthEventKind::bind: inventory_.bind_view(e.device,e.command,e.resource,e.description,e.view); break;
    case DepthEventKind::unbind: inventory_.unbind(e.device,e.command); break;
    case DepthEventKind::draw: inventory_.draw(e.device,e.command,e.elements,e.instances,e.indirect); break;
    case DepthEventKind::clear: inventory_.clear({e.device,e.resource},e.description,e.clear_value,e.has_view?&e.view:nullptr); break;
    case DepthEventKind::begin_effects: inventory_.begin_effects(e.device,e.width,e.height); break;
    case DepthEventKind::end_effects: inventory_.end_effects(e.device); break;
    default: break;
    }
}
size_t DepthObserver::drain(size_t budget) noexcept {
    const auto pending=queue_.queued()-queue_.processed();
    peak_pending_at_drain_=std::max(peak_pending_at_drain_,pending);
    if(!pending || !budget) return 0;
    const auto start=Clock::now();
    size_t count=0; DepthEvent event;
    // Bound each pass even if producers are continuously publishing.
    const auto limit=std::min(budget,queue_capacity);
    while(count<limit && queue_.pop(event)) { apply(event); ++count; }
    const auto elapsed=elapsed_us(start);
    ++drain_batches_;
    max_batch_events_=std::max(max_batch_events_,uint64_t(count));
    total_drain_us_+=elapsed; max_drain_us_=std::max(max_drain_us_,elapsed);
    return count;
}
DepthObserverSnapshot DepthObserver::snapshot() const {
    const auto start=Clock::now();
    DepthObserverSnapshot result;
    result.devices=inventory_.reports(); result.overflow=inventory_.overflow();
    result.processed=queue_.processed(); result.queued=queue_.queued();
    result.pending=result.queued-result.processed;
    result.peak_pending_at_drain=peak_pending_at_drain_;
    result.full_events=full_events_.load(std::memory_order_relaxed);
    result.contended_events=contended_events_.load(std::memory_order_relaxed);
    result.callback_failures=callback_failures_.load(std::memory_order_relaxed);
    result.deferred_events=deferred_events_.load(std::memory_order_relaxed);
    for(size_t i=0;i<rejected_by_kind_.size();++i) result.rejected_by_kind[i]=rejected_by_kind_[i].load(std::memory_order_relaxed);
    result.drain_batches=drain_batches_; result.max_batch_events=max_batch_events_;
    result.total_drain_us=total_drain_us_; result.max_drain_us=max_drain_us_;
    result.snapshot_build_us=elapsed_us(start);
    return result;
}
}
