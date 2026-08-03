#include "core/image.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace fei {

namespace {

std::string lowercase_extension(const AssetPath& path) {
    auto extension = path.path().extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension;
}

} // namespace

std::string_view ImageImporter::name() const {
    return "image";
}

std::uint32_t ImageImporter::version() const {
    return 3;
}

std::span<const std::string_view> ImageImporter::extensions() const {
    static constexpr std::array extensions = {
        std::string_view(".png"),
        std::string_view(".jpg"),
        std::string_view(".jpeg"),
        std::string_view(".bmp"),
        std::string_view(".tga"),
        std::string_view(".hdr"),
    };
    return extensions;
}

AssetImportSettings
ImageImporter::default_settings(const AssetPath& /*destination*/) const {
    return {
        {"color_space", "linear"},
        {"generate_mipmaps", "false"},
    };
}

Status<std::string> ImageImporter::validate(
    const Reader& source,
    const AssetImportContext& context
) const {
    const auto extension = lowercase_extension(context.destination);
    const auto color_space = context.settings.find("color_space");
    auto image = decode_image(
        std::span(source.data(), source.size()),
        ImageDecodeOptions {
            .flip_vertically = extension != ".hdr",
            .hdr = extension == ".hdr",
            .srgb = color_space != context.settings.end() &&
                    color_space->second == "srgb",
        }
    );
    if (!image) {
        return failure(std::move(image.error()));
    }
    return {};
}

Result<AssetImportArtifacts, std::string> ImageImporter::import(
    const Reader& source,
    const AssetImportContext& context
) const {
    const auto extension = lowercase_extension(context.destination);
    const auto color_space = context.settings.find("color_space");
    auto image = decode_image(
        std::span(source.data(), source.size()),
        ImageDecodeOptions {
            .flip_vertically = extension != ".hdr",
            .hdr = extension == ".hdr",
            .srgb = color_space != context.settings.end() &&
                    color_space->second == "srgb",
        }
    );
    if (!image) {
        return failure(std::move(image.error()));
    }

    const auto artifact = context.artifact_directory / "image.bin";
    auto status = write_image_artifact(**image, artifact);
    if (!status) {
        return failure(std::move(status.error()));
    }
    return AssetImportArtifacts {
        AssetArtifact {.kind = "image", .path = "image.bin"},
    };
}

} // namespace fei
