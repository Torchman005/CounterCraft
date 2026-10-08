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
        std::cout<<"capture control parsing, restart, sequence, capacity and replay checks passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
