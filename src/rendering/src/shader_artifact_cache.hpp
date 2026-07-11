#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace fei {

struct ShaderCompileOutput;
struct ShaderCompileRequest;
struct ShaderVariantCompileOutput;

class ShaderArtifactCache {
  private:
    std::filesystem::path m_root;
    std::string m_compiler_identity;

  public:
    struct Key {
        std::uint64_t value;
        std::filesystem::path path;
    };

    ShaderArtifactCache(
        std::filesystem::path root,
        std::string compiler_identity
    );

    [[nodiscard]] std::optional<Key>
    key(const ShaderCompileRequest& request) const;

    [[nodiscard]] std::optional<ShaderVariantCompileOutput>
    load(const Key& key) const;

    void store(
        const Key& key,
        const ShaderCompileRequest& request,
        const ShaderCompileOutput& output
    ) const;
};

} // namespace fei
