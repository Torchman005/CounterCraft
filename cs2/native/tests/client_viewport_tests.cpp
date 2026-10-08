#include "client_viewport.hpp"
#include <stdexcept>
#include <limits>
void require(bool condition) { if(!condition) throw std::runtime_error("Client aspect fit failed"); }
int main() {
    auto wide = cc::fit_client(1680,1050,1280,720);
    require(wide.width == 1680 && wide.height == 945);
    auto tall = cc::fit_client(1920,1080,960,960);
    require(tall.width == 1080 && tall.height == 1080);
    auto same = cc::fit_client(960,540,1280,720);
    require(same.width == 960 && same.height == 540);
    // A 10% pointer move must land at 10% of guest content, excluding the bars.
    require(94.5 / wide.height == .1);
    for(double invalid : {0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        require(cc::fit_client(invalid,1080,1280,720).width == 0);
        require(cc::fit_client(1920,1080,1280,invalid).height == 0);
    }
}
