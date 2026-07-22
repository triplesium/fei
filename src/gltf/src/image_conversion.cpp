#include "image_conversion.hpp"

#include "base/result.hpp"
#include "core/image.hpp"
#include "graphics/enums.hpp"
#include "graphics/sampler.hpp"

#include <cstddef>
#include <cstdint>
#include <fastgltf/types.hpp>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fei::gltf_detail {
namespace {

Result<std::span<const std::byte>, std::string>
buffer_bytes(const fastgltf::Asset& asset, std::size_t buffer_index) {
    if (buffer_index >= asset.buffers.size()) {
        return failure(std::string("references an invalid buffer"));
    }

    const auto& source = asset.buffers[buffer_index].data;
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&source)) {
        return std::span(array->bytes.data(), array->bytes.size());
    }
    if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&source)) {
        return std::span(vector->bytes.data(), vector->bytes.size());
    }
    if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&source)) {
        return std::span(view->bytes.data(), view->bytes.size());
    }
    return failure(
        std::string("references buffer data that has not been loaded")
    );
}

Result<std::span<const std::byte>, std::string> image_bytes(
    const fastgltf::Asset& asset,
    const fastgltf::Image& image,
    std::size_t image_index
) {
    const auto label = "glTF image " + std::to_string(image_index);
    if (const auto* source =
            std::get_if<fastgltf::sources::Array>(&image.data)) {
        return std::span(source->bytes.data(), source->bytes.size());
    }
    if (const auto* source =
            std::get_if<fastgltf::sources::Vector>(&image.data)) {
        return std::span(source->bytes.data(), source->bytes.size());
    }
    const auto* source =
        std::get_if<fastgltf::sources::BufferView>(&image.data);
    if (source == nullptr) {
        return failure(label + " data has not been loaded");
    }
    if (source->bufferViewIndex >= asset.bufferViews.size()) {
        return failure(label + " references an invalid buffer view");
    }

    const auto& buffer_view = asset.bufferViews[source->bufferViewIndex];
    auto bytes = buffer_bytes(asset, buffer_view.bufferIndex);
    if (!bytes) {
        return failure(label + " " + std::move(bytes.error()));
    }
    if (buffer_view.byteOffset > bytes->size() ||
        buffer_view.byteLength > bytes->size() - buffer_view.byteOffset) {
        return failure(label + " buffer view is out of bounds");
    }
    return bytes->subspan(buffer_view.byteOffset, buffer_view.byteLength);
}

SamplerAddressMode convert_wrap(fastgltf::Wrap wrap) {
    switch (wrap) {
        case fastgltf::Wrap::ClampToEdge:
            return SamplerAddressMode::ClampToEdge;
        case fastgltf::Wrap::MirroredRepeat:
            return SamplerAddressMode::MirrorRepeat;
        case fastgltf::Wrap::Repeat:
            return SamplerAddressMode::Repeat;
    }
    return SamplerAddressMode::Repeat;
}

void apply_min_filter(SamplerDescription& sampler, fastgltf::Filter filter) {
    switch (filter) {
        case fastgltf::Filter::Nearest:
        case fastgltf::Filter::NearestMipMapNearest:
            sampler.min_filter = SamplerFilter::Nearest;
            sampler.mipmap_filter = SamplerFilter::Nearest;
            break;
        case fastgltf::Filter::Linear:
        case fastgltf::Filter::LinearMipMapNearest:
            sampler.min_filter = SamplerFilter::Linear;
            sampler.mipmap_filter = SamplerFilter::Nearest;
            break;
        case fastgltf::Filter::NearestMipMapLinear:
            sampler.min_filter = SamplerFilter::Nearest;
            sampler.mipmap_filter = SamplerFilter::Linear;
            break;
        case fastgltf::Filter::LinearMipMapLinear:
            sampler.min_filter = SamplerFilter::Linear;
            sampler.mipmap_filter = SamplerFilter::Linear;
            break;
    }
}

Result<SamplerDescription, std::string> convert_sampler(
    const fastgltf::Asset& asset,
    const fastgltf::Texture& texture,
    std::size_t texture_index
) {
    auto sampler = SamplerDescription::Linear;
    if (!texture.samplerIndex) {
        return sampler;
    }
    if (*texture.samplerIndex >= asset.samplers.size()) {
        return failure(
            "glTF texture " + std::to_string(texture_index) +
            " references an invalid sampler"
        );
    }
    const auto& source = asset.samplers[*texture.samplerIndex];
    sampler.address_mode_u = convert_wrap(source.wrapS);
    sampler.address_mode_v = convert_wrap(source.wrapT);
    if (source.magFilter) {
        sampler.mag_filter = *source.magFilter == fastgltf::Filter::Nearest ?
                                 SamplerFilter::Nearest :
                                 SamplerFilter::Linear;
    }
    if (source.minFilter) {
        apply_min_filter(sampler, *source.minFilter);
    }
    return sampler;
}

} // namespace

Result<std::vector<std::unique_ptr<Image>>, std::string> convert_textures(
    const fastgltf::Asset& asset,
    std::span<const std::uint8_t> srgb_textures
) {
    if (srgb_textures.size() != asset.textures.size()) {
        return failure(
            std::string("glTF texture color-space table is invalid")
        );
    }
    std::vector<std::unique_ptr<Image>> textures;
    textures.reserve(asset.textures.size());
    for (std::size_t texture_index = 0; texture_index < asset.textures.size();
         ++texture_index) {
        const auto& texture = asset.textures[texture_index];
        if (!texture.imageIndex) {
            return failure(
                "glTF texture " + std::to_string(texture_index) +
                " uses an unsupported image extension"
            );
        }
        if (*texture.imageIndex >= asset.images.size()) {
            return failure(
                "glTF texture " + std::to_string(texture_index) +
                " references an invalid image"
            );
        }
        auto bytes = image_bytes(
            asset,
            asset.images[*texture.imageIndex],
            *texture.imageIndex
        );
        if (!bytes) {
            return failure(std::move(bytes.error()));
        }
        auto sampler = convert_sampler(asset, texture, texture_index);
        if (!sampler) {
            return failure(std::move(sampler.error()));
        }
        auto image = decode_image(
            *bytes,
            ImageDecodeOptions {
                .flip_vertically = false,
                .srgb = srgb_textures[texture_index] != 0,
                .sampler = *sampler,
            }
        );
        if (!image) {
            return failure(
                "glTF texture " + std::to_string(texture_index) + ": " +
                std::move(image.error())
            );
        }
        textures.push_back(std::move(*image));
    }
    return textures;
}

} // namespace fei::gltf_detail
