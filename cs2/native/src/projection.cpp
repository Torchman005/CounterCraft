#include "projection.hpp"
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace cc {
namespace {
double eye_distance(const std::array<double,4>& p, double depth, double sign) {
    const double denominator = depth*p[2]-p[0];
    if (denominator == 0) return std::numeric_limits<double>::infinity();
    return sign*(p[1]-depth*p[3])/denominator;
}
}
ProjectionDepth ProjectionDepth::from_d3d_column_major(const std::array<double,16>& m) {
    for (double v : m) if (!std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
        throw std::runtime_error("Projection must contain GPU-representable finite values");
    // Reject orthographic, oblique, skew, mirrored and mixed view-projection matrices.
    // TAA/off-axis x/y offsets at m[8],m[9] are allowed; z must not depend on x/y.
    for (size_t i : {size_t(1),size_t(2),size_t(3),size_t(4),size_t(6),size_t(7),size_t(12),size_t(13),size_t(15)})
        if (std::abs(m[i]) > 1e-8) throw std::runtime_error("Unsupported perspective matrix layout");
    if (m[0] <= 0 || m[5] <= 0 || std::abs(std::abs(m[11])-1) > 1e-8 || m[14] == 0)
        throw std::runtime_error("Unsupported perspective lens or handedness");
    ProjectionDepth result;
    result.coefficients_ = {m[10],m[14],m[11],m[15]};
    result.clip_coefficients_=result.coefficients_;
    result.sign_ = m[11] > 0 ? 1 : -1;
    const double zero = eye_distance(result.coefficients_,0,result.sign_);
    const double one = eye_distance(result.coefficients_,1,result.sign_);
    if (std::isnan(zero) || std::isnan(one) || zero <= 0 || one <= 0 || zero == one)
        throw std::runtime_error("Projection depth endpoints must be in front of the camera");
    result.reversed_ = zero > one;
    result.near_ = result.reversed_ ? one : zero;
    result.far_ = result.reversed_ ? zero : one;
    if (!std::isfinite(result.near_) || result.near_ < 1e-9 || result.far_ <= result.near_)
        throw std::runtime_error("Invalid projection planes");
    result.fov_ = 2*std::atan(1/m[5])*180/std::numbers::pi;
    result.aspect_ = m[5]/m[0];
    if (result.fov_ < 1 || result.fov_ > 179 || !std::isfinite(result.aspect_) || result.aspect_ < .01 || result.aspect_ > 100)
        throw std::runtime_error("Unsupported projection field of view/aspect");
    return result;
}
double ProjectionDepth::distance(double depth) const {
    if (!std::isfinite(depth) || depth < 0 || depth > 1) throw std::runtime_error("Window depth outside [0,1]");
    if(clear_ && depth==*clear_) return std::numeric_limits<double>::infinity();
    if(depth<viewport_[0] || depth>viewport_[1]) throw std::runtime_error("Window depth outside viewport range");
    return eye_distance(coefficients_,depth,sign_);
}
ProjectionDepth ProjectionDepth::with_viewport(double lo,double hi) const {
    if(!std::isfinite(lo) || !std::isfinite(hi) || lo<0 || hi>1 || hi-lo<1e-6)
        throw std::runtime_error("Invalid viewport depth range");
    auto result=*this; result.viewport_={lo,hi}; const auto& p=clip_coefficients_;
    // raw = lo + (hi-lo) * (a*z+b)/(c*z+d). Fold this into
    // the inverse coefficients; near/far and handedness remain clip properties.
    result.coefficients_={(hi-lo)*p[0]+lo*p[2],(hi-lo)*p[1]+lo*p[3],p[2],p[3]};
    return result;
}
ProjectionDepth ProjectionDepth::with_clear_value(double clear) const {
    if(!std::isfinite(clear) || clear<0 || clear>1) throw std::runtime_error("Invalid confirmed depth clear");
    auto result=*this; result.clear_=clear; return result;
}
}
