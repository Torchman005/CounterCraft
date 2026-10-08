#pragma once
#include <utility>

namespace cc {
// Event deltas; ignore SDL's synthetic return to the client centre.
struct MouseMotion {
    int x{},y{}; bool seeded{};
    std::pair<int,int> update(int next_x,int next_y,int center_x,int center_y);
    void reset(){seeded=false;}
};
}
