#pragma once
#include "base/optional.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace fei {

class AssetPath {
  private:
    std::filesystem::path m_path;
    Optional<std::string> m_source;

    AssetPath(std::filesystem::path path, Optional<std::string> source) :
        m_path(std::move(path)), m_source(std::move(source)) {}

    static std::filesystem::path normalize(std::filesystem::path path) {
        if (path.empty()) {
            return {};
        }
        auto normalized = path.lexically_normal();
        return normalized == "." ? std::filesystem::path {} : normalized;
    }

  public:
    AssetPath(const std::string& path) {
        static const std::string delimiter = "://";
        size_t pos = path.find(delimiter);

        if (pos != std::string::npos) {
            m_source = path.substr(0, pos);
            m_path = path.substr(pos + delimiter.length());
        } else {
            m_source = nullopt;
            m_path = path;
        }
    }
    AssetPath(const std::filesystem::path& path) :
        AssetPath(path.generic_string()) {}
    AssetPath(const char* path) : AssetPath(std::string(path)) {}

    const Optional<std::string>& source() const { return m_source; }

    const std::filesystem::path& path() const { return m_path; }

    AssetPath normalized() const {
        return AssetPath(normalize(m_path), m_source);
    }

    AssetPath with_source(std::string source) const {
        return AssetPath(normalize(m_path), std::move(source));
    }

    AssetPath resolve(const AssetPath& path) const {
        if (path.source()) {
            return AssetPath(normalize(path.path()), path.source());
        }

        const auto& relative = path.path();
        if (relative.has_root_directory()) {
            return AssetPath(normalize(relative.relative_path()), m_source);
        }
        return AssetPath(normalize(m_path / relative), m_source);
    }

    AssetPath resolve_str(std::string_view path) const {
        return resolve(AssetPath(std::string(path)));
    }

    AssetPath resolve_embed(const AssetPath& path) const {
        if (path.source() || path.path().has_root_directory()) {
            return resolve(path);
        }

        auto base = m_path;
        if (base.has_filename()) {
            base = base.parent_path();
        }
        return AssetPath(normalize(base / path.path()), m_source);
    }

    AssetPath resolve_embed_str(std::string_view path) const {
        return resolve_embed(AssetPath(std::string(path)));
    }

    bool is_unapproved() const {
        if (m_path.is_absolute()) {
            return true;
        }
        const auto normalized = normalize(m_path);
        return !normalized.empty() && *normalized.begin() == "..";
    }

    std::string as_string() const {
        if (m_source) {
            return *m_source + "://" + m_path.generic_string();
        } else {
            return m_path.generic_string();
        }
    }

    bool operator==(const AssetPath& other) const {
        return m_path == other.m_path && m_source == other.m_source;
    }
    bool operator!=(const AssetPath& other) const { return !(*this == other); }
};

} // namespace fei

namespace std {
template<>
struct hash<fei::AssetPath> { // NOLINT(readability-identifier-naming)
    std::size_t operator()(const fei::AssetPath& asset_path) const noexcept {
        return std::hash<std::string>()(asset_path.as_string());
    }
};
} // namespace std
