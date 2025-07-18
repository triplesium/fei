#include "graphics/plugin.hpp"
#include "graphics/device.hpp"
#include "graphics/enums.hpp"
#include "graphics/texture2d.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <fstream>
#include <stb_image.h>

namespace fei {

Texture2D* Texture2DLoader::load_asset(const std::filesystem::path& path) {
    int width, height, nrChannels;
    PixelFormat format;
    stbi_set_flip_vertically_on_load(true);
    auto data =
        stbi_load(path.string().c_str(), &width, &height, &nrChannels, 0);
    if (!data) {
        fei::error("Failed to load image");
        return {};
    }

    if (nrChannels == 4) {
        format = PixelFormat::RGBA8888;
    } else {
        format = PixelFormat::RGB888;
    }

    TextureDescriptor desc {
        .texture_type = TextureType::Texture2D,
        .texture_format = format,
        .texture_usage = TextureUsage::Read,
        .width = width,
        .height = height,
        .sampler_descriptor = {},
        .data = reinterpret_cast<const std::byte*>(data),
    };
    Texture2D* texture = RenderDevice::instance()->create_texture2d(desc);

    stbi_image_free(data);
    return texture;
}

std::string read_file_str(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        fei::error("File not found: {}", path.string());
        return "";
    }
    std::fstream in_file(path);
    std::stringstream ss;
    ss << in_file.rdbuf();
    return ss.str();
}

Program* ProgramLoader::load_asset(const std::filesystem::path& path) {
    using namespace std::string_literals;
    auto device = RenderDevice::instance();
    auto src = read_file_str(path.string());
    auto vertex_shader = device->create_shader(
        ShaderStage::Vertex,
        "#version 330 core\n"s + "#define VERTEX\n"s + src
    );
    auto fragment_shader = device->create_shader(
        ShaderStage::Fragment,
        "#version 330 core\n"s + "#define FRAGMENT\n"s + src
    );

    Program* program = device->create_program(*vertex_shader, *fragment_shader);

    return program;
}

void GraphicsPlugin::setup(App& app) {
    auto& asset_server = app.get_resource<AssetServer>();
    asset_server.add_loader<Program, ProgramLoader>();
    asset_server.add_loader<Texture2D, Texture2DLoader>();
}

} // namespace fei
