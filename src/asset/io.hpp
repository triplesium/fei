#pragma once
#include "base/log.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fei {

class Reader {
  private:
    std::vector<std::byte> m_buffer;
    std::span<const std::byte> m_data;

  public:
    Reader(const std::byte* data, std::size_t size) : m_data(data, size) {}

    Reader(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            error("Failed to open file: {}", path.string());
        }
        auto file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        m_buffer.resize(static_cast<std::size_t>(file_size));
        file.read(reinterpret_cast<char*>(m_buffer.data()), file_size);

        m_data = m_buffer;
    }

    const std::byte* data() const { return m_data.data(); }
    std::size_t size() const { return m_data.size(); }
    std::string_view as_string_view() const {
        return std::string_view(
            reinterpret_cast<const char*>(m_data.data()),
            m_data.size()
        );
    }
    std::string as_string() const {
        return std::string(
            reinterpret_cast<const char*>(m_data.data()),
            m_data.size()
        );
    }
};

} // namespace fei
