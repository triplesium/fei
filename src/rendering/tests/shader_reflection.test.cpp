#include "rendering/shader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;

TEST_CASE(
    "Shader reflection parser extracts resource bindings",
    "[rendering][shader]"
) {
    const std::string json = R"({
        "ubos": [
            {
                "type": "_12",
                "name": "Material",
                "block_size": 24,
                "set": 2,
                "binding": 0
            }
        ],
        "textures": [
            {
                "type": "sampler2D",
                "name": "albedo_map",
                "set": 2,
                "binding": 1,
                "array": [4]
            }
        ],
        "images": [
            {
                "type": "image3D",
                "name": "voxel_radiance",
                "writeonly": true,
                "set": 3,
                "binding": 0
            }
        ]
    })";

    auto bindings = parse_shader_reflection_bindings(json);

    REQUIRE(bindings);
    REQUIRE(bindings->size() == 3);
    REQUIRE((*bindings)[0].name == "Material");
    REQUIRE((*bindings)[0].kind == ResourceKind::UniformBuffer);
    REQUIRE((*bindings)[0].set == 2);
    REQUIRE((*bindings)[0].binding == 0);
    REQUIRE((*bindings)[1].name == "albedo_map");
    REQUIRE((*bindings)[1].kind == ResourceKind::TextureReadOnly);
    REQUIRE((*bindings)[1].set == 2);
    REQUIRE((*bindings)[1].binding == 1);
    REQUIRE((*bindings)[1].array_size == 4);
    REQUIRE((*bindings)[2].name == "voxel_radiance");
    REQUIRE((*bindings)[2].kind == ResourceKind::TextureReadWrite);
    REQUIRE((*bindings)[2].set == 3);
    REQUIRE((*bindings)[2].binding == 0);
}

TEST_CASE(
    "Shader reflection parser returns errors for invalid reflection JSON",
    "[rendering][shader]"
) {
    SECTION("invalid JSON") {
        auto bindings = parse_shader_reflection_bindings("{");

        REQUIRE_FALSE(bindings);
        REQUIRE(bindings.error().contains("Invalid shader reflection JSON"));
    }

    SECTION("non-object root") {
        auto bindings = parse_shader_reflection_bindings("[]");

        REQUIRE_FALSE(bindings);
        REQUIRE(bindings.error().contains(
            "Shader reflection root is not a JSON object"
        ));
    }

    SECTION("resource field is not an array") {
        auto bindings = parse_shader_reflection_bindings(R"({
            "textures": {}
        })");

        REQUIRE_FALSE(bindings);
        REQUIRE(bindings.error().contains(
            "Shader reflection field 'textures' is not an array"
        ));
    }
}

TEST_CASE(
    "ShaderLoader returns errors for invalid shader metadata",
    "[rendering][shader]"
) {
    App app;
    AssetServer server(&app);
    LoadContext context(server, AssetPath("shader://broken.vert"));
    ShaderLoader loader;

    SECTION("invalid manifest") {
        Reader reader(std::string_view("format=bad\n"));

        auto shader = loader.load(reader, context);

        REQUIRE_FALSE(shader);
        REQUIRE(shader.error().path.as_string() == "shader://broken.vert");
        REQUIRE(shader.error().message.contains("Invalid shader manifest"));
    }

    SECTION("missing generated artifact") {
        const std::string manifest = "format=fei.shader.v1\n"
                                     "logical=broken.vert\n"
                                     "opengl=missing.vert.glsl\n"
                                     "spirv=missing.vert.spv\n"
                                     "reflection=missing.vert.json\n";
        Reader reader(std::string_view(manifest.data(), manifest.size()));

        auto shader = loader.load(reader, context);

        REQUIRE_FALSE(shader);
        REQUIRE(shader.error().path.as_string() == "shader://broken.vert");
        REQUIRE(shader.error().message.contains(
            "Generated OpenGL shader is missing"
        ));
    }
}
