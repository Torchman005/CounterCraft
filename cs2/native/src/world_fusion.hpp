#pragma once
#include <filesystem>
#include <memory>
#include <json.hpp>
#include <reshade.hpp>

namespace cc {
class Receiver;
struct Frame;
class WorldFusion {
public:
    WorldFusion(bool,const std::filesystem::path&,Receiver&);
    ~WorldFusion();
    void install();
    void uninstall();
    // Called at the protected ReShade effect boundary; never draws into host state.
    void begin(reshade::api::effect_runtime*,reshade::api::command_list*);
    bool bind(reshade::api::effect_runtime*,const Frame&);
    void release(reshade::api::effect_runtime*);
    nlohmann::json report() const;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};
}
