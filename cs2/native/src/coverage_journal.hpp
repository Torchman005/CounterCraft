#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cc {
// Value-only history for one latched world snapshot. Capture callbacks run
// BEFORE a resource is overwritten; no texture references survive the epoch.
class CoverageJournal {
public:
    static constexpr size_t capacity=16;
    static constexpr uint32_t capture_limit=32;
    struct Entry {uint64_t resource{};bool cleared{},dirty{};};
    void reset(){entries_={};size_=0;captures_=0;active_=false;}
    void begin(uint64_t world){reset();if(!world)throw std::runtime_error("Missing coverage world");active_=true;write(world);}
    void write(uint64_t resource){if(active_)entry(resource).dirty=true;}
    bool contains(uint64_t resource)const {
        for(size_t i=0;i<size_;++i)if(entries_[i].resource==resource)return true;
        return false;
    }
    bool cleared(uint64_t resource)const {
        for(size_t i=0;i<size_;++i)if(entries_[i].resource==resource)return entries_[i].cleared;
        return false;
    }
    template<class Capture> void capture(uint64_t resource,Capture&& callback,bool force=false){
        if(!active_)return;
        for(size_t i=0;i<size_;++i)if(entries_[i].resource==resource){
            auto& e=entries_[i];if(!e.dirty && !force)return;
            if(captures_==capture_limit)throw std::runtime_error("Too many post-world depth captures");
            ++captures_;callback(e.resource,e.cleared);e.dirty=false;return;
        }
    }
    template<class Capture> void clear(uint64_t resource,bool known_full_clear,Capture&& callback){
        if(!active_)return;
        if(!known_full_clear)throw std::runtime_error("Unknown post-world depth clear");
        auto& e=entry(resource);capture(resource,callback);e.cleared=true;e.dirty=false;
    }
    template<class Capture> void copy(uint64_t destination,Capture&& callback){
        if(!active_)return;
        auto& e=entry(destination);capture(destination,callback);e.cleared=false;e.dirty=true;
    }
    template<class Capture> void flush(Capture&& callback){
        for(size_t i=0;i<size_;++i)capture(entries_[i].resource,callback);
    }
private:
    Entry& entry(uint64_t resource){
        if(!resource)throw std::runtime_error("Missing coverage resource");
        for(size_t i=0;i<size_;++i)if(entries_[i].resource==resource)return entries_[i];
        if(size_==capacity)throw std::runtime_error("Too many post-world depth resources");
        entries_[size_].resource=resource;return entries_[size_++];
    }
    std::array<Entry,capacity> entries_{};
    size_t size_{};
    uint32_t captures_{};
    bool active_{};
};
}
