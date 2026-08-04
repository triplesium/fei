#pragma once

#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "base/types.hpp"

#include <cstddef>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fei {

class Image;

class ImGuiTextureHandle {
  public:
    ImGuiTextureHandle() = default;

    [[nodiscard]] ImTextureID texture_id() const noexcept {
        return static_cast<ImTextureID>(m_texture_id);
    }
    [[nodiscard]] uint64 raw_id() const noexcept { return m_texture_id; }
    [[nodiscard]] bool is_valid() const noexcept { return m_texture_id != 0; }
    explicit operator bool() const noexcept { return is_valid(); }

    bool operator==(const ImGuiTextureHandle&) const = default;

  private:
    friend class ImGuiImages;
    friend class ImGuiRenderTextures;

    explicit ImGuiTextureHandle(uint64 texture_id) : m_texture_id(texture_id) {}

    uint64 m_texture_id {0};
};

struct ImGuiImageBinding {
    uint64 texture_id {0};
    AssetId image_id {invalid_asset_id};
};

struct ExtractedImGuiImages {
    std::vector<ImGuiImageBinding> bindings;
};

class ImGuiImages {
  public:
    [[nodiscard]] ImGuiTextureHandle register_image(Handle<Image> image);
    bool unregister_image(ImGuiTextureHandle texture);

    [[nodiscard]] bool contains(ImGuiTextureHandle texture) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] ExtractedImGuiImages extract() const;

  private:
    std::unordered_map<uint64, Handle<Image>> m_images;
    uint64 m_next_id {1};
};

// Allocates stable ImGui IDs for textures produced inside the Render World.
// The Main World owns only these handles; the renderer owns their GPU bindings.
class ImGuiRenderTextures {
  public:
    [[nodiscard]] ImGuiTextureHandle reserve_texture();
    bool release_texture(ImGuiTextureHandle texture);

    [[nodiscard]] bool contains(ImGuiTextureHandle texture) const;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    std::unordered_set<uint64> m_textures;
    uint64 m_next_id {1};
};

} // namespace fei
