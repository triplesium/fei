#include "asset/io.hpp"

#include <cstring>
#include <fstream>
#include <utility>

namespace fei {

ReaderError::ReaderError(std::filesystem::path path, std::string message) :
    path(std::move(path)), message(std::move(message)) {}

Reader::Reader(std::vector<std::byte> buffer) :
    m_buffer(std::move(buffer)), m_owns_data(true) {
    bind_owned_data();
}

void Reader::bind_owned_data() {
    if (m_owns_data) {
        m_data = m_buffer;
    }
}

Reader::Reader(const std::byte* data, std::size_t size) : m_data(data, size) {}

Reader::Reader(std::string_view text) {
    m_buffer.resize(text.size());
    if (!text.empty()) {
        std::memcpy(m_buffer.data(), text.data(), text.size());
    }
    m_owns_data = true;
    bind_owned_data();
}

Reader::Reader(const Reader& other) :
    m_buffer(other.m_buffer), m_data(other.m_data),
    m_owns_data(other.m_owns_data) {
    bind_owned_data();
}

Reader::Reader(Reader&& other) noexcept :
    m_buffer(std::move(other.m_buffer)), m_data(other.m_data),
    m_owns_data(other.m_owns_data) {
    bind_owned_data();
    other.m_data = {};
    other.m_owns_data = false;
}

Reader& Reader::operator=(const Reader& other) {
    if (this == &other) {
        return *this;
    }
    m_buffer = other.m_buffer;
    m_data = other.m_data;
    m_owns_data = other.m_owns_data;
    bind_owned_data();
    return *this;
}

Reader& Reader::operator=(Reader&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    m_buffer = std::move(other.m_buffer);
    m_data = other.m_data;
    m_owns_data = other.m_owns_data;
    bind_owned_data();
    other.m_data = {};
    other.m_owns_data = false;
    return *this;
}

Result<Reader, ReaderError>
Reader::from_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return failure(
            ReaderError(path, "Failed to open file: " + path.string())
        );
    }

    auto file_size = file.tellg();
    if (file_size < 0) {
        return failure(
            ReaderError(path, "Failed to determine file size: " + path.string())
        );
    }

    file.seekg(0, std::ios::beg);
    if (!file) {
        return failure(
            ReaderError(path, "Failed to seek file: " + path.string())
        );
    }

    std::vector<std::byte> buffer(static_cast<std::size_t>(file_size));
    if (!buffer.empty()) {
        file.read(
            reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size())
        );
        if (!file) {
            return failure(
                ReaderError(path, "Failed to read file: " + path.string())
            );
        }
    }

    return Reader(std::move(buffer));
}

const std::byte* Reader::data() const {
    return m_data.data();
}

std::size_t Reader::size() const {
    return m_data.size();
}

std::string_view Reader::as_string_view() const {
    return std::string_view(
        reinterpret_cast<const char*>(m_data.data()),
        m_data.size()
    );
}

std::string Reader::as_string() const {
    return std::string(
        reinterpret_cast<const char*>(m_data.data()),
        m_data.size()
    );
}

} // namespace fei
