#include "base/log.hpp"
#include "imgui/texture.hpp"

#include <limits>

namespace fei {

namespace {

constexpr uint64 image_texture_id_mask = uint64 {1} << 62;
constexpr uint64 image_texture_sequence_mask = image_texture_id_mask - 1;
constexpr uint64 render_texture_id_mask = uint64 {1} << 61;
constexpr uint64 render_texture_sequence_mask = render_texture_id_mask - 1;

} // namespace

ImGuiTextureHandle ImGuiImages::register_image(Handle<Image> image) {
    static_assert(std::numeric_limits<ImTextureID>::digits >= 64);
    if (!image) {
        fatal("ImGuiImages cannot register an invalid image handle");
    }
    if (m_next_id > image_texture_sequence_mask) {
        fatal("ImGuiImages exhausted texture IDs");
    }

    const auto texture_id = image_texture_id_mask | m_next_id++;
    m_images.emplace(texture_id, std::move(image));
    return ImGuiTextureHandle(texture_id);
}

bool ImGuiImages::unregister_image(ImGuiTextureHandle texture) {
    return m_images.erase(texture.raw_id()) != 0;
}

bool ImGuiImages::contains(ImGuiTextureHandle texture) const {
    return m_images.contains(texture.raw_id());
}

std::size_t ImGuiImages::size() const noexcept {
    return m_images.size();
}

ExtractedImGuiImages ImGuiImages::extract() const {
    ExtractedImGuiImages extracted;
    extracted.bindings.reserve(m_images.size());
    for (const auto& [texture_id, image] : m_images) {
        extracted.bindings.push_back(
            ImGuiImageBinding {
                .texture_id = texture_id,
                .image_id = image.id(),
            }
        );
    }
    return extracted;
}

ImGuiTextureHandle ImGuiRenderTextures::reserve_texture() {
    if (m_next_id > render_texture_sequence_mask) {
        fatal("ImGuiRenderTextures exhausted texture IDs");
    }
    const auto texture_id = render_texture_id_mask | m_next_id++;
    m_textures.insert(texture_id);
    return ImGuiTextureHandle(texture_id);
}

bool ImGuiRenderTextures::release_texture(ImGuiTextureHandle texture) {
    return m_textures.erase(texture.raw_id()) != 0;
}

bool ImGuiRenderTextures::contains(ImGuiTextureHandle texture) const {
    return m_textures.contains(texture.raw_id());
}

std::size_t ImGuiRenderTextures::size() const noexcept {
    return m_textures.size();
}

} // namespace fei
