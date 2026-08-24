#include "runtime_host/snapshot_archive.hpp"

#include "asset/reference.hpp"
#include "project/project.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#elif defined(__APPLE__)
#    include <mach-o/dyld.h>
#endif

namespace ets::runtime_host {
namespace {

class StableDigest {
  private:
    std::uint64_t m_hash {14695981039346656037ULL};

    void append_byte(std::uint8_t value) {
        m_hash ^= value;
        m_hash *= 1099511628211ULL;
    }

  public:
    void append(std::string_view value) {
        char size[32];
        const auto [end, error] =
            std::to_chars(std::begin(size), std::end(size), value.size());
        if (error == std::errc {}) {
            for (auto cursor = std::begin(size); cursor != end; ++cursor) {
                append_byte(static_cast<std::uint8_t>(*cursor));
            }
        }
        append_byte(0xffU);
        for (const auto character : value) {
            append_byte(static_cast<std::uint8_t>(character));
        }
        append_byte(0xfeU);
    }

    std::string finish() const {
        char buffer[17];
        const auto [end, error] =
            std::to_chars(std::begin(buffer), std::end(buffer), m_hash, 16);
        return error == std::errc {} ? std::string(buffer, end) :
                                       std::string("invalid");
    }
};

void append_reference(StableDigest& digest, const AssetReference& reference) {
    digest.append(reference.id ? reference.id->as_string() : "no-uuid");
    digest.append(reference.fallback_path.as_string());
}

bool is_script_file(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".lua" || extension == ".luau";
}

Result<std::string, std::string>
script_digest(const std::filesystem::path& asset_root) {
    StableDigest digest;
    digest.append("entisium.project-scripts.v1");

    std::error_code error;
    if (!std::filesystem::exists(asset_root, error)) {
        if (error) {
            return failure(
                "Failed to inspect project asset directory '" +
                asset_root.string() + "': " + error.message()
            );
        }
        return digest.finish();
    }

    std::vector<std::filesystem::path> scripts;
    std::filesystem::recursive_directory_iterator iterator(asset_root, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && !error &&
            is_script_file(iterator->path())) {
            scripts.push_back(iterator->path());
        }
        iterator.increment(error);
    }
    if (error) {
        return failure(
            "Failed to enumerate project scripts in '" + asset_root.string() +
            "': " + error.message()
        );
    }
    std::ranges::sort(scripts, {}, [&asset_root](const auto& path) {
        return path.lexically_relative(asset_root).generic_string();
    });
    for (const auto& path : scripts) {
        const auto relative = path.lexically_relative(asset_root);
        digest.append(relative.generic_string());
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return failure(
                "Failed to read project script '" + path.string() + "'"
            );
        }
        const std::string content {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
        if (!stream.eof() && stream.fail()) {
            return failure(
                "Failed to read project script '" + path.string() + "'"
            );
        }
        digest.append(content);
    }
    return digest.finish();
}

Result<std::filesystem::path, std::string> current_executable_path() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (length == 0 || length >= buffer.size()) {
        return failure(
            "Failed to resolve the runtime executable path (Windows error " +
            std::to_string(GetLastError()) + ")"
        );
    }
    buffer.resize(length);
    return std::filesystem::path(std::move(buffer));
#elif defined(__linux__)
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return failure(
            "Failed to resolve the runtime executable path: " + error.message()
        );
    }
    return path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return failure(
            std::string("Failed to resolve the runtime executable path")
        );
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return std::filesystem::path(std::move(buffer));
#else
    return failure(
        std::string(
            "Runtime executable discovery is unsupported on this platform"
        )
    );
#endif
}

} // namespace

Result<std::string, std::string> current_runtime_build_id() {
    auto path = current_executable_path();
    if (!path) {
        return failure(std::move(path.error()));
    }
    std::ifstream stream(*path, std::ios::binary);
    if (!stream) {
        return failure(
            "Failed to read runtime executable '" + path->string() + "'"
        );
    }

    StableDigest digest;
    digest.append("entisium.runtime-executable.v1");
    std::array<char, 64 * 1024> buffer {};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            digest.append(
                std::string_view(buffer.data(), static_cast<std::size_t>(count))
            );
        }
    }
    if (!stream.eof()) {
        return failure(
            "Failed to hash runtime executable '" + path->string() + "'"
        );
    }
    return "entisium-runtime-host:" + digest.finish();
}

Result<snapshot::SnapshotArchiveMetadata, std::string>
make_snapshot_archive_metadata(
    const Project& project,
    std::string engine_build
) {
    if (engine_build.empty()) {
        engine_build = c_default_snapshot_engine_build;
    }

    StableDigest runtime;
    runtime.append("entisium.project-runtime.v1");
    runtime.append(project.config().name);
    runtime.append(project.config().asset_directory.generic_string());
    for (const auto& plugin : project.config().runtime.plugins) {
        runtime.append(plugin.qualified_name());
    }
    for (const auto& script : project.config().scripts) {
        append_reference(runtime, script);
    }
    auto scripts = script_digest(project.asset_root());
    if (!scripts) {
        return failure(std::move(scripts.error()));
    }
    return snapshot::SnapshotArchiveMetadata {
        .project = project.config().name,
        .engine_build = std::move(engine_build),
        .runtime_signature = runtime.finish(),
        .script_hash = std::move(*scripts),
    };
}

} // namespace ets::runtime_host
