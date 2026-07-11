#include "snapshot_types.hpp"

#include "devtools/json.hpp"
#include "refl/generated.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::rendering;

namespace {

void register_snapshot_test_types() {
    static bool registered = false;
    if (!registered) {
        register_generated_reflection();
        registered = true;
    }
}

} // namespace

TEST_CASE(
    "Rendering snapshot DTOs preserve render graph debug data",
    "[devtools][rendering][snapshot]"
) {
    register_snapshot_test_types();

    RgDebugInfo debug;
    debug.compiled = true;
    debug.active_pass_names = {"lighting"};
    debug.stats = RenderGraphStats {
        .total_passes = 3,
        .active_passes = 1,
        .culled_passes = 2,
        .transient_texture_requests = 4,
        .transient_texture_hits = 3,
        .transient_texture_creates = 1,
        .texture_pool_size = 5,
    };
    debug.passes.push_back(
        RgPassDebugInfo {
            .index = 2,
            .name = "lighting",
            .active = true,
            .side_effect = true,
            .dependencies = {0, 1},
            .reads =
                {
                    RgTextureUseDebugInfo {
                        .handle = RgTextureHandle {.index = 4, .generation = 6},
                        .texture_name = "gbuffer",
                        .access = RenderGraphAccess::TextureRead,
                        .access_name = "texture_read",
                    },
                },
            .writes = {
                RgTextureUseDebugInfo {
                    .handle = RgTextureHandle {.index = 7, .generation = 8},
                    .texture_name = "lighting_output",
                    .access = RenderGraphAccess::ColorAttachmentWrite,
                    .access_name = "color_attachment_write",
                },
            },
        }
    );
    debug.textures.push_back(
        RgTextureDebugInfo {
            .index = 7,
            .name = "lighting_output",
            .active = true,
            .imported = false,
            .width = 1280,
            .height = 720,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .format = "rgba8_unorm",
            .usage = "color_attachment",
            .type = "2d",
            .sample_count = 1,
            .version_count = 2,
            .first_active_use = 2,
            .last_active_use = 2,
        }
    );
    debug.resource_sets.push_back(
        RgResourceSetDebugInfo {
            .index = 3,
            .generation = 9,
            .pass_index = 2,
            .name = "lighting_resources",
            .active = true,
            .resolved = true,
            .has_layout = true,
            .bindings = {
                RgResourceSetBindingDebugInfo {
                    .index = 0,
                    .kind = "texture",
                    .resource_name = "gbuffer",
                    .valid = true,
                    .texture = RgTextureHandle {.index = 4, .generation = 6},
                },
                RgResourceSetBindingDebugInfo {
                    .index = 1,
                    .kind = "buffer",
                    .resource_name = "lights",
                    .valid = true,
                },
            },
        }
    );

    auto snapshot = make_render_graph_snapshot(debug);
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.compiled);
    REQUIRE(snapshot.total_passes == 3);
    REQUIRE(snapshot.active_order == std::vector<std::string> {"lighting"});
    REQUIRE(snapshot.passes.size() == 1);
    REQUIRE(snapshot.passes[0].reads[0].generation == 6);
    REQUIRE(snapshot.textures[0].width == 1280);
    REQUIRE(snapshot.resource_sets[0].bindings.size() == 2);
    REQUIRE(snapshot.resource_sets[0].bindings[0].has_texture);
    REQUIRE(snapshot.resource_sets[0].bindings[0].texture_index == 4);
    REQUIRE_FALSE(snapshot.resource_sets[0].bindings[1].has_texture);
    REQUIRE(
        snapshot.resource_sets[0].bindings[1].texture_index ==
        RgTextureHandle::InvalidIndex
    );

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("available":true)") != std::string::npos);
    REQUIRE(json->find(R"("has_texture":false)") != std::string::npos);
    REQUIRE(json->find("$type") == std::string::npos);
}

TEST_CASE(
    "Rendering snapshot DTOs preserve graphics cache statistics",
    "[devtools][rendering][snapshot]"
) {
    register_snapshot_test_types();

    GraphicsResourceCacheStats stats {
        .framebuffer_requests = 10,
        .framebuffer_hits = 8,
        .framebuffer_creates = 2,
        .resource_set_requests = 20,
        .resource_set_hits = 15,
        .resource_set_creates = 5,
        .framebuffer_cache_size = 3,
        .resource_set_cache_size = 4,
        .resource_set_sources = {
            GraphicsResourceSetSourceStats {
                .name = "lighting",
                .requests = 6,
                .hits = 4,
                .creates = 2,
                .cache_size = 2,
            },
        },
    };

    auto snapshot = make_graphics_cache_snapshot(stats);
    REQUIRE(snapshot.framebuffer_requests == 10);
    REQUIRE(snapshot.resource_set_hits == 15);
    REQUIRE(snapshot.resource_set_sources.size() == 1);
    REQUIRE(snapshot.resource_set_sources[0].name == "lighting");

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    REQUIRE(json->find(R"("framebuffer_requests":10)") != std::string::npos);
    REQUIRE(json->find(R"("resource_set_sources":[{)") != std::string::npos);
}
