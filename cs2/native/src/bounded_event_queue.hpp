#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cc {
enum class QueuePush { accepted, full, contended };

// Multiple producers, one consumer. A producer makes at most eight reservation
// attempts; it never waits for another producer or for the consumer. Sequence
// publication keeps partially written payloads invisible to the consumer.
template<class T, size_t Capacity> class BoundedEventQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity-1)) == 0);
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    struct Slot { std::atomic<uint64_t> sequence{}; T value{}; };
public:
    BoundedEventQueue() noexcept {
        for(size_t i=0;i<Capacity;++i) slots_[i].sequence.store(i,std::memory_order_relaxed);
    }
    QueuePush push(const T& value) noexcept {
        auto position=write_.load(std::memory_order_relaxed);
        for(unsigned attempt=0;attempt<8;++attempt) {
            auto& slot=slots_[position & (Capacity-1)];
            const auto sequence=slot.sequence.load(std::memory_order_acquire);
            if(sequence==position) {
                if(write_.compare_exchange_strong(position,position+1,std::memory_order_relaxed)) {
                    slot.value=value;
                    slot.sequence.store(position+1,std::memory_order_release);
                    return QueuePush::accepted;
                }
            } else if(sequence<position) {
                return QueuePush::full;
            } else {
                position=write_.load(std::memory_order_relaxed);
            }
        }
        return QueuePush::contended;
    }
    // Only the consumer may call pop/processed. A reserved but unpublished head
    // is temporarily unavailable; it is never skipped or reordered.
    bool pop(T& value) noexcept {
        auto& slot=slots_[read_ & (Capacity-1)];
        if(slot.sequence.load(std::memory_order_acquire)!=read_+1) return false;
        value=slot.value;
        slot.sequence.store(read_+Capacity,std::memory_order_release);
        ++read_;
        return true;
    }
    uint64_t queued() const noexcept { return write_.load(std::memory_order_relaxed); }
    uint64_t processed() const noexcept { return read_; }
    static constexpr size_t capacity=Capacity;
private:
    std::array<Slot,Capacity> slots_{};
    std::atomic<uint64_t> write_{};
    // An explicit gap avoids producer/consumer false sharing without requiring
    // implicit over-alignment padding in every containing class on MSVC.
    std::array<std::byte,64> cursor_gap_{};
    uint64_t read_{};
};
}
