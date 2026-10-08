#include "receiver.hpp"
#include <json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

// Exercised by an independent Python socket server; no GPU or game required.
int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) throw std::runtime_error("Usage: receiver_probe port seconds [camera]");
        const auto port = std::stoul(argv[1]);
        const double seconds = std::stod(argv[2]);
        if (!port || port > 65535 || seconds <= 0 || seconds > 10) throw std::runtime_error("Probe range");
        const auto mode=argc==4?std::string(argv[3]):std::string{};
        const bool ui=mode=="ui" || mode=="ui-stale";
        const bool gameplay=mode=="gameplay" || ui;
        cc::Receiver receiver(uint16_t(port), 20,gameplay);
        const auto began = cc::monotonic_ns();
        uint64_t observed = 0, last = 0;
        bool saw_depth = false;
        unsigned camera_stage=0;
        bool ui_issued=false;
        while (double(cc::monotonic_ns() - began) / 1e9 < seconds) {
            const auto elapsed=double(cc::monotonic_ns()-began)/1e9;
            if(ui && !ui_issued && receiver.stats().connected && elapsed>.08) {
                auto now=cc::monotonic_ns();
                receiver.submit_ui({"hello",0,0,now-(mode=="ui-stale"?500'000'000:0)});
                if(mode=="ui")receiver.submit_ui({"",257,0,now});
                ui_issued=true;
            }
            if(gameplay && elapsed>.08 && elapsed<.35) {
                cc::Receiver::Input input;input.forward=1;input.captured_ns=cc::monotonic_ns();receiver.submit_input(input);
            }
            if(argc==4 && !gameplay && ((camera_stage==0 && elapsed>.08) || (camera_stage==1 && elapsed>.16))) {
                cc::HostCamera camera;camera.right_handed=true;camera.fov=camera_stage?80:70;
                camera.position=camera_stage?std::array<double,3>{132,136,364}:std::array<double,3>{100,200,300};
                camera.yaw=camera_stage?90:0;camera.pitch=camera_stage?-20:10;
                receiver.submit_camera(camera,++camera_stage,cc::monotonic_ns());
            }
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
            {"camerasSent",status.cameras_sent},{"cameraReleases",status.camera_releases},
            {"cameraFramesMatched",status.cameras_rendered},
            {"inputsSent",status.inputs_sent},{"reconnects",status.reconnects},
            {"uiEventsSent",status.ui_sent},{"uiEventsDropped",status.ui_dropped},
            {"stopMs",double(cc::monotonic_ns()-stop_at)/1e6}}.dump() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
