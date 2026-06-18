#include "rendering/shader.hpp"

#include <catch2/catch_test_macros.hpp>
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

    REQUIRE(bindings.size() == 3);
    REQUIRE(bindings[0].name == "Material");
    REQUIRE(bindings[0].kind == ResourceKind::UniformBuffer);
    REQUIRE(bindings[0].set == 2);
    REQUIRE(bindings[0].binding == 0);
    REQUIRE(bindings[1].name == "albedo_map");
    REQUIRE(bindings[1].kind == ResourceKind::TextureReadOnly);
    REQUIRE(bindings[1].set == 2);
    REQUIRE(bindings[1].binding == 1);
    REQUIRE(bindings[1].array_size == 4);
    REQUIRE(bindings[2].name == "voxel_radiance");
    REQUIRE(bindings[2].kind == ResourceKind::TextureReadWrite);
    REQUIRE(bindings[2].set == 3);
    REQUIRE(bindings[2].binding == 0);
}
