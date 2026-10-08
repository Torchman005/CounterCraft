// Local evidence runner for the production decoder. Never reads process memory.
#include "host_camera.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs=std::filesystem;
using json=nlohmann::json;
json manifest(const fs::path& path) {
    if(fs::file_size(path)>65536) throw std::runtime_error("Manifest too large");
    std::ifstream input(path); return json::parse(input);
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("Usage: camera_probe calibration.json capture.json");
        const auto layout=cc::CameraLayout::parse(manifest(argv[1]));
        const fs::path capture=fs::absolute(argv[2]); const auto m=manifest(capture);
        if(m.at("schema")!=1 || m.at("timing")!="before-current-draw" || m.at("viewports").size()!=1)
            throw std::runtime_error("Unsupported capture");
        const auto descriptors=m.at("constantBuffers");
        if(!descriptors.is_array() || descriptors.size()>28) throw std::runtime_error("Invalid binding list");
        std::vector<std::vector<uint8_t>> storage; storage.reserve(14);
        std::vector<cc::CameraBuffer> buffers;
        for(const auto& d:descriptors) {
            if(d.at("stage")!="VS" || d.contains("skipped")) continue;
            const auto name=d.at("file").get<std::string>();
            if(name.find_first_of("/\\:")!=std::string::npos || name=="." || name=="..") throw std::runtime_error("Nonlocal buffer path");
            const auto bytes=d.at("bytes").get<uint32_t>(); const auto path=capture.parent_path()/name;
            if(!bytes || bytes>65536 || fs::file_size(path)!=bytes) throw std::runtime_error("Invalid buffer length");
            storage.emplace_back(bytes); std::ifstream input(path,std::ios::binary);
            input.read(reinterpret_cast<char*>(storage.back().data()),bytes);
            if(!input) throw std::runtime_error("Buffer read failed");
            const bool partial=d.contains("firstConstant") && !d.at("firstConstant").is_null();
            buffers.push_back({d.at("slot").get<uint32_t>(),partial?d.at("firstConstant").get<uint32_t>():0,
                partial?d.at("numConstants").get<uint32_t>():4096,storage.back()});
        }
        const auto result=cc::decode_host_camera(layout,buffers,m.at("viewports")[0].get<std::array<double,6>>());
        std::cout<<result.report().dump(2)<<'\n'; return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
