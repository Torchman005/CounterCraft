#include "projection.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* reason) { if(!value) throw std::runtime_error(reason); }
void close(double a,double b) { require(std::abs(a-b)<1e-9*std::max(1.,std::abs(b)),"Projection distance mismatch"); }
void rejects(const std::function<void()>& fn) { try{fn();}catch(const std::exception&){return;} throw std::runtime_error("Invalid projection accepted"); }
std::array<double,16> fixture(double a,double b,double c) {
    return {1.125,0,0,0, 0,2,0,0, 0,0,a,c, 0,0,b,0};
}
}
int main() {
    try {
        // Hand-derived endpoints: n=1, f=101; sampled geometry is at z=2.
        for(bool rh:{false,true}) for(bool reversed:{false,true}) {
            auto m=fixture((reversed ? -.01 : 1.01)*(rh ? -1 : 1),reversed ? 1.01 : -1.01,rh ? -1 : 1);
            auto p=cc::ProjectionDepth::from_d3d_column_major(m);
            close(p.near_plane(),1); close(p.far_plane(),101); close(p.aspect(),16./9.);
            require(p.reversed()==reversed && p.right_handed()==rh,"Convention mismatch");
            close(p.distance(reversed ? 1 : 0),1); close(p.distance(reversed ? 0 : 1),101);
            close(p.distance(reversed ? .495 : .505),2);
        }
        for(bool rh:{false,true}) for(bool reversed:{false,true}) {
            auto p=cc::ProjectionDepth::from_d3d_column_major(fixture(reversed ? 0 : (rh ? -1 : 1),reversed ? 1 : -1,rh ? -1 : 1));
            require(std::isinf(p.far_plane()) && std::isinf(p.distance(reversed ? 0 : 1)),"Infinite far endpoint");
            close(p.near_plane(),1); close(p.distance(reversed ? .2 : .8),5);
        }
        auto jitter=fixture(1.01,-1.01,1); jitter[8]=.001; jitter[9]=-.002;
        close(cc::ProjectionDepth::from_d3d_column_major(jitter).distance(.505),2);
        auto base=fixture(1.01,-1.01,1);
        for(size_t i:{size_t(1),size_t(2),size_t(3),size_t(4),size_t(6),size_t(7),size_t(12),size_t(13),size_t(15)}) {
            auto m=base; m[i]=.1; rejects([&]{cc::ProjectionDepth::from_d3d_column_major(m);});
        }
        for(auto replacement:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            auto m=base;m[0]=replacement;rejects([&]{cc::ProjectionDepth::from_d3d_column_major(m);});
        }
        auto transposed=base; std::swap(transposed[11],transposed[14]);
        rejects([&]{cc::ProjectionDepth::from_d3d_column_major(transposed);});
        auto behind=fixture(1.01,1.01,1); rejects([&]{cc::ProjectionDepth::from_d3d_column_major(behind);});
        auto p=cc::ProjectionDepth::from_d3d_column_major(base);
        for(double invalid:{-.001,1.001,std::numeric_limits<double>::quiet_NaN()}) rejects([&]{p.distance(invalid);});
        for(auto& v:base) v=double(float(v));
        require(std::abs(cc::ProjectionDepth::from_d3d_column_major(base).distance(.505)-2)<1e-5,"FP32 input lens");
        std::cout<<"{\"projectionTests\":\"passed\",\"groups\":6}\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
