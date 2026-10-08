#include "depth_observer.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace cc;
void require(bool value,const char* reason) { if(!value) throw std::runtime_error(reason); }
DepthEvent event(DepthEventKind kind,uint64_t dev=1,uint64_t resource=1,uint64_t command=1) {
    return {.kind=kind,.device=dev,.command=command,.resource=resource};
}
DepthDescription description() { return {1280,720,1,1,4,44,DepthKind::d24s8,false}; }
DepthEvent binding(uint64_t resource=1) {
    auto e=event(DepthEventKind::bind,1,resource); e.description=description();
    e.view=normalize_depth_view(DepthViewType::texture_2d_multisample,45,0,1,0,0,e.description,true);
    return e;
}
DepthEvent drawing(uint32_t count=36,bool indirect=false) {
    auto e=event(DepthEventKind::draw); e.elements=count; e.instances=1; e.indirect=indirect; return e;
}
DepthEvent begin() {
    auto e=event(DepthEventKind::begin_effects); e.width=1280; e.height=720; return e;
}
void submit(DepthObserver& observer,const DepthEvent& e) { require(observer.submit(e),"Unexpected rejection"); }
const DepthReport& first(const DepthObserverSnapshot& s) {
    for(const auto& d:s.devices) if(d.device_id) return d;
    throw std::runtime_error("Missing device");
}
void accounting(const DepthObserverSnapshot& s) {
    require(s.queued==s.processed+s.pending,"Processed prefix and queued tail accounting");
    for(const auto& d:s.devices) for(uint32_t i=0;i<d.count;++i) {
        const auto& c=d.candidates[i].counts; uint64_t draws=0,clears=0,elements=0,indirect=0;
        for(uint32_t j=0;j<c.view_count;++j) {
            draws+=c.views[j].draws; clears+=c.views[j].clears;
            elements+=c.views[j].elements; indirect+=c.views[j].indirect;
        }
        require(draws==c.draws && clears==c.clears && elements==c.elements && indirect==c.indirect,
            "Concurrent report must not tear aggregate/per-view statistics");
    }
}
void test_ring() {
    BoundedEventQueue<uint64_t,4> q;
    uint64_t value=0;
    require(!q.pop(value),"Empty queue");
    for(uint64_t round=0;round<10000;++round) {
        for(uint64_t i=0;i<4;++i) require(q.push(round*4+i)==QueuePush::accepted,"Fill ring");
        require(q.push(999)==QueuePush::full,"Full ring refuses overwrite");
        for(uint64_t i=0;i<4;++i) require(q.pop(value) && value==round*4+i,"FIFO across ring wrap");
        require(!q.pop(value),"Empty after wrap drain");
    }
    require(q.queued()==40000 && q.processed()==40000,"Queue counters exclude rejected writes");
}
void test_replay() {
    auto p=std::make_unique<DepthObserver>();
    submit(*p,binding()); submit(*p,drawing()); submit(*p,drawing(2,true));
    auto clear=event(DepthEventKind::clear); clear.description=description();
    clear.view=binding().view; clear.has_view=true; clear.clear_value=1;
    submit(*p,clear); submit(*p,begin());
    submit(*p,binding()); submit(*p,drawing(999)); submit(*p,clear);
    submit(*p,event(DepthEventKind::end_effects));
    require(p->drain(2)==2,"Budget bounds consumer work");
    auto s=p->snapshot(); require(s.processed==2 && s.pending==7,"Partial drain is visible");
    require(p->drain()==7,"Drain pending FIFO tail"); s=p->snapshot(); accounting(s);
    auto d=first(s);
    require(d.interval==1 && d.count==1 && d.candidates[0].counts.draws==3
        && d.candidates[0].counts.elements==36 && d.candidates[0].counts.indirect==2
        && d.candidates[0].counts.clears==1,"Effects excluded and indirect counts preserved");
    require(s.known_loss_free() && s.pending==0,"No gaps in accepted replay");
    const auto id=d.candidates[0].id;
    submit(*p,event(DepthEventKind::destroy_resource));
    auto created=event(DepthEventKind::observe); created.description=description(); created.new_lifetime=true;
    submit(*p,created); submit(*p,drawing()); submit(*p,begin());
    submit(*p,event(DepthEventKind::end_effects)); p->drain();
    require(first(p->snapshot()).count==0,"Destroyed binding cannot survive handle reuse");
    submit(*p,binding()); submit(*p,drawing()); submit(*p,begin());
    submit(*p,event(DepthEventKind::end_effects)); p->drain();
    require(first(p->snapshot()).candidates[0].id!=id,"New lifetime gets new candidate identity");
    submit(*p,event(DepthEventKind::unbind)); submit(*p,drawing()); submit(*p,begin());
    submit(*p,event(DepthEventKind::end_effects)); p->drain();
    require(first(p->snapshot()).count==0,"Explicit unbind prevents attribution");
    submit(*p,event(DepthEventKind::destroy_device)); p->drain();
    s=p->snapshot(); require(s.pending==0 && s.devices[0].device_id==0,"Shutdown drains and clears device");
}
void test_capacity() {
    auto p=std::make_unique<DepthObserver>();
    // The consumer may be encoding/logging a report. Producers still fill every
    // free slot independently, then record a visible gap without overwriting.
    for(size_t i=0;i<DepthObserver::queue_capacity;++i) submit(*p,drawing());
    require(!p->submit(begin()),"Full queue rejects effect boundary");
    auto s=p->snapshot();
    require(s.pending==DepthObserver::queue_capacity && s.full_events==1 && s.missed()==1
        && s.rejected_by_kind[size_t(DepthEventKind::begin_effects)]==1 && !s.known_loss_free(),"Overflow is explicit by event kind");
    require(p->drain()==DepthObserver::queue_capacity,"Capacity bounds full-tail drain");
    submit(*p,begin()); submit(*p,event(DepthEventKind::end_effects)); p->drain();
    p->failed(); p->deferred();
    s=p->snapshot();
    require(s.pending==0 && s.missed()==2 && s.callback_failures==1 && s.deferred_events==1
        && s.peak_pending_at_drain==DepthObserver::queue_capacity,"Gap counters survive recovery");
}
void test_multi_producer() {
    struct Payload { uint64_t producer{}, serial{}, check{}; };
    auto q=std::make_unique<BoundedEventQueue<Payload,1024>>();
    constexpr uint64_t threads=4, count=50000;
    std::array<uint64_t,threads> accepted{},full{},contended{},seen{},last{};
    std::atomic<unsigned> done{};
    std::vector<std::thread> producers;
    for(uint64_t t=0;t<threads;++t) producers.emplace_back([&,t] {
        for(uint64_t i=1;i<=count;++i) {
            const Payload payload{t,i,(t+1)*0x100000000ULL+i};
            switch(q->push(payload)) {
            case QueuePush::accepted: ++accepted[t]; break;
            case QueuePush::full: ++full[t]; break;
            case QueuePush::contended: ++contended[t]; break;
            }
            if(i%64==0) std::this_thread::yield();
        }
        done.fetch_add(1,std::memory_order_release);
    });
    bool valid=true; Payload value;
    while(done.load(std::memory_order_acquire)!=threads || q->processed()!=q->queued()) {
        if(!q->pop(value)) { std::this_thread::yield(); continue; }
        if(value.producer>=threads) { valid=false; continue; }
        valid=valid && value.serial>last[value.producer] && value.check==(value.producer+1)*0x100000000ULL+value.serial;
        last[value.producer]=value.serial; ++seen[value.producer];
    }
    for(auto& t:producers) t.join();
    require(valid,"Concurrent payload publication and per-producer FIFO");
    for(size_t t=0;t<threads;++t) require(seen[t]==accepted[t] && accepted[t]+full[t]+contended[t]==count,
        "Each attempted event is consumed exactly once or explicitly rejected");
}
void test_reporting_overlap() {
    auto p=std::make_unique<DepthObserver>(); std::atomic<bool> done{};
    constexpr uint64_t frames=10000;
    std::thread producer([&] {
        for(uint64_t i=0;i<frames;++i) {
            p->submit(binding()); p->submit(drawing()); p->submit(begin());
            p->submit(event(DepthEventKind::end_effects));
            if(i%16==0) std::this_thread::yield();
        }
        done.store(true,std::memory_order_release);
    });
    bool valid=true; uint64_t snapshots=0;
    while(!done.load(std::memory_order_acquire)) {
        p->drain(256);
        try { accounting(p->snapshot()); } catch(...) { valid=false; }
        ++snapshots;
    }
    producer.join(); while(p->drain()) {}
    const auto s=p->snapshot(); accounting(s);
    require(valid && snapshots>0,"Snapshot/producer overlap is consistent");
    require(s.pending==0 && s.contended_events==0 && s.queued+s.full_events==frames*4,
        "Single producer never contends with consumer or report work");
    require(s.drain_batches>0 && s.max_batch_events<=DepthObserver::queue_capacity,"Bounded consumer telemetry");
}
}
int main() {
    try {
        test_ring(); test_replay(); test_capacity(); test_multi_producer(); test_reporting_overlap();
        std::cout << "Depth observer: 5 concurrency/replay/accounting groups passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
