#include "receiver.hpp"
#include <json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

// Exercised by an independent Python socket server; no GPU or game required.
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: receiver_probe port seconds");
        const auto port = std::stoul(argv[1]);
        const double seconds = std::stod(argv[2]);
        if (!port || port > 65535 || seconds <= 0 || seconds > 10) throw std::runtime_error("Probe range");
        cc::Receiver receiver(uint16_t(port), 20);
        const auto began = cc::monotonic_ns();
        uint64_t observed = 0, last = 0;
        bool saw_depth = false;
        while (double(cc::monotonic_ns() - began) / 1e9 < seconds) {
            if (auto frame = receiver.latest()) {
                if (frame->header.sequence != last) {
                    ++observed; last = frame->header.sequence;
                    saw_depth = frame->rgba().size() == 8 && frame->depth().size() == 8;
                }
            }
            if (!receiver.stats().failure.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const bool latest = bool(receiver.latest());
        const auto status = receiver.stats();
        const auto stop_at = cc::monotonic_ns();
        receiver.stop();
        std::cout << nlohmann::json{{"received",status.received},{"stale",status.stale},
            {"observed",observed},{"last",last},{"payload",saw_depth},{"latest",latest},
            {"failure",status.failure},{"cleared",!receiver.latest()},
            {"stopMs",double(cc::monotonic_ns()-stop_at)/1e6}}.dump() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
