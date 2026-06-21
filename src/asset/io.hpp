#pragma once
#include "base/result.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fei {

struct ReaderError {
    std::filesystem::path path;
    std::string message;

    ReaderError(std::filesystem::path path, std::string message);
};

class Reader {
  private:
    std::vector<std::byte> m_buffer;
    std::span<const std::byte> m_data;
    bool m_owns_data {false};

    explicit Reader(std::vector<std::byte> buffer);

    void bind_owned_data();

  public:
    Reader(const std::byte* data, std::size_t size);
    explicit Reader(std::string_view text);
    Reader(const std::filesystem::path& path);
    Reader(const Reader& other);
    Reader(Reader&& other) noexcept;
    Reader& operator=(const Reader& other);
    Reader& operator=(Reader&& other) noexcept;

    static Result<Reader, ReaderError>
    try_from_file(const std::filesystem::path& path);

    const std::byte* data() const;
    std::size_t size() const;
    std::string_view as_string_view() const;
    std::string as_string() const;
};

} // namespace fei
