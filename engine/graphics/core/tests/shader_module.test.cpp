#include "graphics/shader_module.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

using namespace ets;

namespace {

ShaderResourceBinding
uniform_binding(std::string name, uint32 set, uint32 binding) {
    return ShaderResourceBinding {
        .name = std::move(name),
        .kind = ResourceKind::UniformBuffer,
        .set = set,
        .binding = binding,
    };
}

} // namespace

TEST_CASE(
    "Shader resource reflection merges stages and preserves sets",
    "[graphics][shader][reflection]"
) {
    std::array shaders {
        ShaderDescription {
            .stage = ShaderStages::Vertex,
            .resources = {uniform_binding("frame", 1, 2)},
        },
        ShaderDescription {
            .stage = ShaderStages::Fragment,
            .resources = {uniform_binding("frame", 1, 2)},
        },
    };

    auto layouts = reflect_resource_layouts(shaders);

    REQUIRE(layouts.has_value());
    REQUIRE(layouts->size() == 2);
    CHECK((*layouts)[0].elements.empty());
    REQUIRE((*layouts)[1].elements.size() == 1);
    const auto& reflected = (*layouts)[1].elements[0];
    CHECK(reflected.name == "frame");
    CHECK(reflected.binding == 2);
    CHECK(reflected.kind == ResourceKind::UniformBuffer);
    CHECK(reflected.stages.is_set(ShaderStages::Vertex));
    CHECK(reflected.stages.is_set(ShaderStages::Fragment));
}

TEST_CASE(
    "Shader resource reflection rejects conflicting bindings",
    "[graphics][shader][reflection]"
) {
    auto vertex_binding = uniform_binding("frame", 0, 0);
    auto fragment_binding = uniform_binding("image", 0, 0);
    fragment_binding.kind = ResourceKind::TextureReadOnly;
    std::array shaders {
        ShaderDescription {
            .stage = ShaderStages::Vertex,
            .resources = {std::move(vertex_binding)},
        },
        ShaderDescription {
            .stage = ShaderStages::Fragment,
            .resources = {std::move(fragment_binding)},
        },
    };

    auto layouts = reflect_resource_layouts(shaders);

    REQUIRE_FALSE(layouts.has_value());
    CHECK(layouts.error().message.find("set 0 binding 0") != std::string::npos);
}
