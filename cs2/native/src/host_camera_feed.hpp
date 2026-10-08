#pragma once
#include <filesystem>
#include <memory>
#include <json.hpp>

namespace cc {
class Receiver;
class HostCameraFeed {
public:
    HostCameraFeed(bool,const std::filesystem::path&,Receiver&);
    ~HostCameraFeed();
    void install();
    void uninstall();
    nlohmann::json report() const;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
}
