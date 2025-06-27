#pragma once

#include <cstdint>
#include <tuple>

namespace fei {
struct Color3B;
struct Color4B;
struct Color4F;

struct Color3B {
    std::uint8_t r {0}, g {0}, b {0};

    Color3B() = default;
    Color3B(std::uint8_t r, std::uint8_t g, std::uint8_t b) :
        r {r}, g {g}, b {b} {}
};

struct Color4B {
    std::uint8_t r {0}, g {0}, b {0}, a {0};

    Color4B() = default;
    Color4B(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) :
        r {r}, g {g}, b {b}, a {a} {}
};

struct Color4F {
    float r {.0f}, g {.0f}, b {.0f}, a {.0f};

    Color4F() = default;
    Color4F(float r, float g, float b, float a) : r {r}, g {g}, b {b}, a {a} {}
    Color4F(const Color4B& color) :
        r {color.a / 255.0f}, g {color.g / 255.0f}, b {color.b / 255.0f},
        a {color.a / 255.0f} {}

    std::tuple<float, float, float, float> values() const {
        return {r, g, b, a};
    }

    static const Color4F White;
    static const Color4F Yellow;
    static const Color4F Green;
    static const Color4F Blue;
    static const Color4F Red;
    static const Color4F Magenta;
    static const Color4F Black;
    static const Color4F Orange;
    static const Color4F Gray;
};
} // namespace fei
