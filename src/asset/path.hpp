#pragma once
#include "base/optional.hpp"

#include <filesystem>
#include <string>

namespace fei {

class AssetPath {
  private:
    std::filesystem::path m_path;
    Optional<std::string> m_source;

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
    AssetPath(const char* path) : AssetPath(std::string(path)) {}

    const Optional<std::string>& source() const { return m_source; }

    const std::filesystem::path& path() const { return m_path; }

    std::string as_string() const {
        if (m_source) {
            return *m_source + "://" + m_path.string();
        } else {
            return m_path.string();
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
struct hash<fei::AssetPath> {
    std::size_t operator()(const fei::AssetPath& asset_path) const noexcept {
        return std::hash<std::string>()(asset_path.as_string());
    }
};
} // namespace std
