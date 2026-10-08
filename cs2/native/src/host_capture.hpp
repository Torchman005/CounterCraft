#pragma once
#include <json.hpp>
#include <filesystem>
#include <memory>

namespace cc {
// Explicit, limited offline evidence capture. Registration and GPU callbacks are
// isolated from the metadata observer. A separate writer receives CPU bytes only.
class HostCapture {
public:
    HostCapture(bool enabled,const std::filesystem::path& addon_directory);
    ~HostCapture();
    void install();
    void uninstall();
    nlohmann::json report() const;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
}
