#pragma once
#include <array>

namespace cc {
// Explicit D3D [0,1] clip depth, column-major matrix, canonical perspective only.
// A mathematically valid matrix is not evidence that it is CS2's rendered camera.
class ProjectionDepth {
public:
    static ProjectionDepth from_d3d_column_major(const std::array<double,16>&);
    double distance(double window_depth) const;
    const std::array<double,4>& coefficients() const { return coefficients_; }
    double near_plane() const { return near_; }
    double far_plane() const { return far_; } // Infinity is permitted.
    double vertical_fov() const { return fov_; }
    double aspect() const { return aspect_; }
    bool reversed() const { return reversed_; }
    bool right_handed() const { return sign_ < 0; }
private:
    ProjectionDepth() = default;
    std::array<double,4> coefficients_{}; // clip z = a*z+b, clip w = c*z+d
    double near_{}, far_{}, fov_{}, aspect_{}, sign_{};
    bool reversed_{};
};
}
