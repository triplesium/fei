#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace fei {

constexpr float POS_INFINITY = std::numeric_limits<float>::infinity();
constexpr float NEG_INFINITY = -std::numeric_limits<float>::infinity();
constexpr float PI = 3.14159265358979323846264338327950288f;
constexpr float ONE_OVER_PI = 1.0f / PI;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float LOG2 = 0.30102999566f;
constexpr float EPSILON = 1e-6f;
constexpr float FLOAT_EPSILON = 1.192092896e-07f;
constexpr float DOUBLE_EPSILON = 2.2204460492503131e-016;

using std::abs;
using std::acos;
using std::asin;
using std::atan;
using std::clamp;
using std::cos;
using std::isnan;
using std::max;
using std::min;
using std::sin;
using std::sqrt;
using std::tan;

inline float sqr(float value) {
    return value * value;
}
inline float inv_sqrt(float value) {
    return 1.f / sqrt(value);
}
inline bool real_equal(
    float a,
    float b,
    float tolerance = std::numeric_limits<float>::epsilon()
) {
    return abs(b - a) <= tolerance;
}

} // namespace fei
