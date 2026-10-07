#include "frame.hpp"
#include <json.hpp>
#include <bit>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace cc;
namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch(const std::exception&) { return; }
    throw std::runtime_error("Invalid input accepted");
}
std::array<uint8_t,64> header(Session s,uint64_t sequence=1) {
    std::array<uint8_t,64> bytes{}; std::copy_n("CCFRM001",8,bytes.begin());
    auto put=[&](size_t offset,uint64_t value,size_t count=4){for(size_t i=0;i<count;++i)bytes[offset+i]=uint8_t(value>>(i*8));};
    put(8,1);put(12,64);put(16,2);put(20,8);put(24,8);put(32,s.high,8);put(40,s.low,8);put(48,sequence,8);
    return bytes;
}
nlohmann::json metadata() {
    return {{"v",1},{"type","world-stream-frame"},{"epoch",7},{"width",1},{"height",2},
        {"rowOrder","bottom-to-top"},{"colorEncoding","rgba8"},{"depthEncoding","float32-le"},
        {"depthSpace","opengl-window-z"},{"reversedZ",false},{"includesHandHud",false},{"includesSkyFog",true},
        {"near",.05},{"far",768.},{"requestedFrame",-1},{"monotonicNanos",monotonic_ns()},{"readbackNanos",1234},
        {"camera",{{"position",{0,64,0}},{"rotation",{90,30,0}},{"fov",55}}},
        {"projectionColumnMajor",std::array<double,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}},
        {"viewRotationColumnMajor",std::array<double,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};
}
}
int main() {
    try {
        const Session s=Session::parse("01234567-89ab-cdef-fedc-ba9876543210");
        require(s.high==0x0123456789abcdefULL && s.low==0xfedcba9876543210ULL,"UUID layout");
        require(s.str()=="01234567-89ab-cdef-fedc-ba9876543210","UUID round trip");
        rejects([]{Session::parse("01234567-89ab-cdef-fedc-ba987654321z");});
        auto bytes=header(s); auto h=decode_header(bytes,s,0);
        require(h.color_bytes==8 && h.sequence==1,"Binary layout");
        rejects([&]{decode_header(bytes,{0,0},0);}); rejects([&]{decode_header(bytes,s,1);});
        rejects([&]{decode_header(std::span(bytes).first(63),s,0);});
        for(auto offset:{0,8,12,24,28,60}) {
            auto invalid=bytes;invalid[size_t(offset)]^=1;rejects([&]{decode_header(invalid,s,0);});
        }
        auto oversized=bytes;oversized[16]=1;oversized[17]=16;rejects([&]{decode_header(oversized,s,0);});
        const std::string known="123456789";std::span raw(reinterpret_cast<const uint8_t*>(known.data()),known.size());
        require(crc32(raw)==0xcbf43926,"IEEE CRC");require(crc32(raw.subspan(3),crc32(raw.first(3)))==crc32(raw),"Incremental CRC");
        auto j=metadata(); auto m=decode_metadata(j.dump(),h,7);require(m.height==2 && m.rotation[0]==90,"Metadata decode");
        rejects([&]{decode_metadata(j.dump(),h,8);});
        for(auto field:{"rowOrder","colorEncoding","depthSpace","type"}) {
            auto invalid=j;invalid[field]="wrong";rejects([&]{decode_metadata(invalid.dump(),h,7);});
        }
        for(auto field:{"reversedZ","includesHandHud","includesSkyFog"}) {
            auto invalid=j;invalid[field]=0;rejects([&]{decode_metadata(invalid.dump(),h,7);});
        }
        auto invalid=j;invalid["width"]=UINT64_MAX;rejects([&]{decode_metadata(invalid.dump(),h,7);});
        invalid=j;invalid["camera"]["position"]={1,2};rejects([&]{decode_metadata(invalid.dump(),h,7);});
        std::vector<uint8_t> depth(8);uint32_t half=std::bit_cast<uint32_t>(.5f),one=std::bit_cast<uint32_t>(1.f);
        for(size_t i=0;i<4;++i){depth[i]=uint8_t(half>>(8*i));depth[i+4]=uint8_t(one>>(8*i));}validate_depth(depth);
        depth[7]=255;rejects([&]{validate_depth(depth);});
        require(std::abs(linear_depth(0,.05,768)-.05)<1e-8,"Near depth");
        require(std::abs(linear_depth(1,.05,768)-768)<1e-5,"Far depth");
        rejects([]{linear_depth(1.01,.05,768);});
        std::cout<<"{\"nativeProtocolTests\":\"passed\",\"groups\":7}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
