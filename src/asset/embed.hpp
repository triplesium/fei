#pragma once
#include "asset/io.hpp"

#include <cstddef>
#include <cstdint>
#include <print>
#include <string_view>
#include <unordered_map>

namespace fei {

class EmbededData {
  private:
    const std::byte* m_begin;
    const std::byte* m_end;

  public:
    EmbededData(const std::byte* begin, const std::byte* end) :
        m_begin(begin), m_end(end) {}
    EmbededData(const uint8_t* begin, const uint8_t* end) :
        m_begin(reinterpret_cast<const std::byte*>(begin)),
        m_end(reinterpret_cast<const std::byte*>(end)) {}
    Reader reader() const {
        return Reader(m_begin, static_cast<std::size_t>(m_end - m_begin));
    }
};

class EmbededAssets {
  private:
    inline static std::unordered_map<std::string_view, EmbededData>
        s_embedded_assets {};

  public:
    static void
    add(std::string_view name, const uint8_t* begin, const uint8_t* end) {
        s_embedded_assets.emplace(name, EmbededData(begin, end));
    }

    static bool has(std::string_view name) {
        return s_embedded_assets.find(name) != s_embedded_assets.end();
    }

    static const EmbededData& get(std::string_view name) {
        return s_embedded_assets.at(name);
    }
};

namespace detail {
struct EmbededAssetsRegistrar {
    EmbededAssetsRegistrar(
        std::string_view name,
        const uint8_t* begin,
        const uint8_t* end
    ) {
        EmbededAssets::add(name, begin, end);
    }
};
} // namespace detail

} // namespace fei

#define EMBED(name, asset_name)                      \
    extern "C" {                                     \
    extern const uint8_t _binary_##name##_start[];   \
    extern const uint8_t _binary_##name##_end[];     \
    }                                                \
    namespace {                                      \
    static const fei::detail::EmbededAssetsRegistrar \
        _embeded_asset_registrar_##name(             \
            asset_name,                              \
            _binary_##name##_start,                  \
            _binary_##name##_end                     \
        );                                           \
    }\
