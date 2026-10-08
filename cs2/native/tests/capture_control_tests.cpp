#include "capture_control.hpp"
#include <iostream>

void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() {
    try {
        using namespace cc;
        for(const auto* raw:{"{}","[]","{\"manual\":false,\"request\":1}","{\"manual\":true,\"request\":true}",
            "{\"manual\":true,\"request\":-1}","{\"manual\":true,\"request\":1.5}","{\"manual\":true,\"request\":1000001}"}) {
            bool rejected=false;
            try { capture_sequence(nlohmann::json::parse(raw)); } catch(const std::runtime_error&) { rejected=true; }
            require(rejected,"Invalid control must not arm a capture");
        }
        require(capture_sequence(nlohmann::json::parse("{\"manual\":true,\"request\":0}"))==0,"Zero baseline");
        CaptureRequests requests; requests.initialize(7);
        require(!requests.take(true),"Restart must not replay old requests");
        requests.observe(10);
        require(!requests.take(false),"Capacity limit must leave requests unconsumed");
        for(uint64_t expected=8;expected<=10;++expected) {
            require(requests.take(true)==expected,"Rapid requests must get distinct intervals");
            requests.observe(2); // a stale file cannot rewind either side
        }
        require(!requests.take(true),"A request is consumed exactly once, even without eligible draws");
        requests.observe(10); require(!requests.take(true),"Repeated reads cannot replay");
        requests.observe(11); require(requests.take(true)==11,"A new request can arm again");
        require(!capture_transitions(nlohmann::json::object()),"Existing captures keep default policy");
        require(capture_transitions({{"trigger","viewport-transitions"}}),"Transition policy is explicit");
        ViewportCaptureSchedule schedule;
        const ViewportCaptureSchedule::Viewport world{0,0,100,80,0,.95f},hand{0,0,100,80,0,.1f};
        require(!schedule.observe(1,world),"First viewport isn't a boundary");
        require(schedule.observe(64,world),"Initial camera context");
        require(!schedule.observe(256,world),"Stable viewport doesn't repeat capture");
        require(schedule.observe(300,hand) && schedule.transitions==1,"Enter changed depth range");
        require(!schedule.observe(301,hand),"Stable hand range");
        require(schedule.observe(400,world) && schedule.transitions==2,"Return to world range");
        schedule.reset();
        require(!schedule.observe(1,hand) && schedule.transitions==0,"No boundaries across intervals");
        std::cout<<"capture control parsing, restart, sequence, capacity and replay checks passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
