#pragma once

#include "math/common.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <sstream>
#include <string>

namespace fei::test {

using Catch::Matchers::WithinAbs;

inline bool within_abs(float actual, float expected, float margin) {
    return Catch::Matchers::WithinAbs(expected, margin).match(actual);
}

class Vector2WithinAbsMatcher : public Catch::Matchers::MatcherBase<Vector2> {
  public:
    Vector2WithinAbsMatcher(float x, float y, float margin) :
        m_expected(x, y), m_margin(margin) {}

    bool match(const Vector2& actual) const override {
        return within_abs(actual.x, m_expected.x, m_margin) &&
               within_abs(actual.y, m_expected.y, m_margin);
    }

    std::string describe() const override {
        std::ostringstream stream;
        stream << "is vector within abs margin " << m_margin << " of ("
               << m_expected.x << ", " << m_expected.y << ")";
        return stream.str();
    }

  private:
    Vector2 m_expected;
    float m_margin;
};

class Vector3WithinAbsMatcher : public Catch::Matchers::MatcherBase<Vector3> {
  public:
    Vector3WithinAbsMatcher(float x, float y, float z, float margin) :
        m_expected(x, y, z), m_margin(margin) {}

    bool match(const Vector3& actual) const override {
        return within_abs(actual.x, m_expected.x, m_margin) &&
               within_abs(actual.y, m_expected.y, m_margin) &&
               within_abs(actual.z, m_expected.z, m_margin);
    }

    std::string describe() const override {
        std::ostringstream stream;
        stream << "is vector within abs margin " << m_margin << " of ("
               << m_expected.x << ", " << m_expected.y << ", " << m_expected.z
               << ")";
        return stream.str();
    }

  private:
    Vector3 m_expected;
    float m_margin;
};

class Vector4WithinAbsMatcher : public Catch::Matchers::MatcherBase<Vector4> {
  public:
    Vector4WithinAbsMatcher(float x, float y, float z, float w, float margin) :
        m_expected(x, y, z, w), m_margin(margin) {}

    bool match(const Vector4& actual) const override {
        return within_abs(actual.x, m_expected.x, m_margin) &&
               within_abs(actual.y, m_expected.y, m_margin) &&
               within_abs(actual.z, m_expected.z, m_margin) &&
               within_abs(actual.w, m_expected.w, m_margin);
    }

    std::string describe() const override {
        std::ostringstream stream;
        stream << "is vector within abs margin " << m_margin << " of ("
               << m_expected.x << ", " << m_expected.y << ", " << m_expected.z
               << ", " << m_expected.w << ")";
        return stream.str();
    }

  private:
    Vector4 m_expected;
    float m_margin;
};

class Matrix3WithinAbsMatcher : public Catch::Matchers::MatcherBase<Matrix3x3> {
  public:
    Matrix3WithinAbsMatcher(Matrix3x3 expected, float margin) :
        m_expected(expected), m_margin(margin) {}

    bool match(const Matrix3x3& actual) const override {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t col = 0; col < 3; ++col) {
                if (!within_abs(
                        actual[row][col],
                        m_expected[row][col],
                        m_margin
                    )) {
                    return false;
                }
            }
        }
        return true;
    }

    std::string describe() const override {
        std::ostringstream stream;
        stream << "is matrix within abs margin " << m_margin << " of (( "
               << m_expected[0][0] << ", " << m_expected[0][1] << ", "
               << m_expected[0][2] << " ), ( " << m_expected[1][0] << ", "
               << m_expected[1][1] << ", " << m_expected[1][2] << " ), ( "
               << m_expected[2][0] << ", " << m_expected[2][1] << ", "
               << m_expected[2][2] << " ))";
        return stream.str();
    }

  private:
    Matrix3x3 m_expected;
    float m_margin;
};

class Matrix4WithinAbsMatcher : public Catch::Matchers::MatcherBase<Matrix4x4> {
  public:
    Matrix4WithinAbsMatcher(Matrix4x4 expected, float margin) :
        m_expected(expected), m_margin(margin) {}

    bool match(const Matrix4x4& actual) const override {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t col = 0; col < 4; ++col) {
                if (!within_abs(
                        actual[row][col],
                        m_expected[row][col],
                        m_margin
                    )) {
                    return false;
                }
            }
        }
        return true;
    }

    std::string describe() const override {
        std::ostringstream stream;
        stream << "is matrix within abs margin " << m_margin << " of (";
        for (std::size_t row = 0; row < 4; ++row) {
            if (row != 0) {
                stream << ", ";
            }
            stream << "( " << m_expected[row][0] << ", " << m_expected[row][1]
                   << ", " << m_expected[row][2] << ", " << m_expected[row][3]
                   << " )";
        }
        stream << ")";
        return stream.str();
    }

  private:
    Matrix4x4 m_expected;
    float m_margin;
};

// Match Catch2's matcher factory naming.
// NOLINTBEGIN(readability-identifier-naming)
inline Vector2WithinAbsMatcher VectorWithinAbs(float x, float y) {
    return {x, y, EPSILON};
}

inline Vector3WithinAbsMatcher VectorWithinAbs(float x, float y, float z) {
    return {x, y, z, EPSILON};
}

inline Vector4WithinAbsMatcher
VectorWithinAbs(float x, float y, float z, float w) {
    return {x, y, z, w, EPSILON};
}

inline Vector2WithinAbsMatcher
VectorWithinAbs(Vector2 expected, float margin = EPSILON) {
    return {expected.x, expected.y, margin};
}

inline Vector3WithinAbsMatcher
VectorWithinAbs(Vector3 expected, float margin = EPSILON) {
    return {expected.x, expected.y, expected.z, margin};
}

inline Vector4WithinAbsMatcher
VectorWithinAbs(Vector4 expected, float margin = EPSILON) {
    return {expected.x, expected.y, expected.z, expected.w, margin};
}

inline Matrix3WithinAbsMatcher
WithinAbs(Matrix3x3 expected, float margin = EPSILON) {
    return {expected, margin};
}

inline Matrix4WithinAbsMatcher
WithinAbs(Matrix4x4 expected, float margin = EPSILON) {
    return {expected, margin};
}
// NOLINTEND(readability-identifier-naming)

} // namespace fei::test
