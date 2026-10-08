#include "mouse_input.hpp"
#include <algorithm>
#include <cstdint>

namespace cc {
std::pair<int,int> MouseMotion::update(int nx,int ny,int cx,int cy) {
    const bool center=nx==cx && ny==cy;
    const int dx=seeded&&!center?int(std::clamp<int64_t>(int64_t(nx)-x,-1000,1000)):0;
    const int dy=seeded&&!center?int(std::clamp<int64_t>(int64_t(ny)-y,-1000,1000)):0;
    x=nx;y=ny;seeded=true;return {dx,dy};
}
}
