#pragma once
#include "frame.hpp"
#include "host_camera.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <deque>
#include <json.hpp>

namespace cc {
struct ReceiverStats {
    uint64_t received{}, replaced{}, stale{};
    bool connected{};
    std::string failure;
    int64_t clock_uncertainty_ns{};
    uint64_t cameras_sent{},camera_releases{};
    uint64_t cameras_rendered{};
    uint64_t inputs_sent{}, reconnects{};
    uint64_t ui_sent{},ui_dropped{};
    std::array<double,3> player_eye{},player_rotation{};
    bool gui_open{};
    nlohmann::json player_status;
};
class Receiver {
public:
    explicit Receiver(uint16_t port = 37122, unsigned fps = 20, bool gameplay = false, bool reconnect = false);
    ~Receiver();
    Receiver(const Receiver&) = delete;
    std::shared_ptr<const Frame> latest() const;
    ReceiverStats stats() const;
    void stop();
    double age_ms(const Frame&) const;
    void submit_camera(const HostCamera&,uint64_t sequence,int64_t captured_ns);
    struct Input { double yaw{},pitch{},forward{},sideways{},mouse_x{.5},mouse_y{.5};
        int slot{}; bool jump{},sneak{},sprint{},attack{},use{},inventory{},escape{},drop{},swap{},pick{}; int64_t scroll{},captured_ns{}; };
    void submit_input(Input);
    struct UiEvent { std::string text; int key{},modifiers{}; int64_t captured_ns{}; };
    void submit_ui(UiEvent);
    void clear_ui();
private:
    void run();
    void session();
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<int64_t> clock_offset_{0};
    uint16_t port_;
    unsigned fps_;
    bool gameplay_{};
    bool reconnect_{};
    mutable std::mutex mutex_;
    std::shared_ptr<const Frame> latest_;
    mutable uint64_t last_observed_{};
    ReceiverStats stats_;
    struct CameraUpdate { HostCamera camera; uint64_t sequence{}; int64_t captured_ns{}; };
    std::optional<CameraUpdate> camera_;
    std::optional<Input> input_;
    std::deque<UiEvent> ui_;
};
}
