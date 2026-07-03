#include "ecs/world.hpp"
#include "pbr/graph_resources.hpp"
#include "pbr/light.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/render_graph.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fei;

namespace {

TextureDescription make_texture_desc(TextureType texture_type) {
    return TextureDescription {
        .width = 16,
        .height = 16,
        .depth = texture_type == TextureType::Texture3D ? 16U : 1U,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba16Float,
        .texture_usage =
            {TextureUsage::Sampled,
             TextureUsage::Storage,
             TextureUsage::RenderTarget},
        .texture_type = texture_type,
    };
}

TextureDescription make_depth_desc() {
    return TextureDescription {
        .width = 16,
        .height = 16,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Depth32Float,
        .texture_usage = TextureUsage::DepthStencil,
        .texture_type = TextureType::Texture2D,
    };
}

const RgPassDebugInfo&
pass_named(const RgDebugInfo& debug, std::string_view name) {
    auto it = std::ranges::find_if(debug.passes, [&](const auto& pass) {
        return pass.name == name;
    });
    REQUIRE(it != debug.passes.end());
    return *it;
}

uint32 pass_index(const RgDebugInfo& debug, std::string_view name) {
    return pass_named(debug, name).index;
}

bool pass_depends_on(const RgPassDebugInfo& pass, uint32 dependency) {
    return std::ranges::find(pass.dependencies, dependency) !=
           pass.dependencies.end();
}

bool pass_reads(const RgPassDebugInfo& pass, std::string_view texture_name) {
    return std::ranges::find_if(pass.reads, [&](const auto& read) {
               return read.texture_name == texture_name;
           }) != pass.reads.end();
}

bool pass_writes(const RgPassDebugInfo& pass, std::string_view texture_name) {
    return std::ranges::find_if(pass.writes, [&](const auto& write) {
               return write.texture_name == texture_name;
           }) != pass.writes.end();
}

bool has_pass_named(const RgDebugInfo& debug, std::string_view name) {
    return std::ranges::find_if(debug.passes, [&](const auto& pass) {
               return pass.name == name;
           }) != debug.passes.end();
}

const RgResourceSetDebugInfo& resource_set_named(
    const RgDebugInfo& debug,
    std::string_view name,
    uint32 pass_index
) {
    auto it = std::ranges::find_if(
        debug.resource_sets,
        [&](const auto& resource_set) {
            return resource_set.name == name &&
                   resource_set.pass_index == pass_index;
        }
    );
    REQUIRE(it != debug.resource_sets.end());
    return *it;
}

bool resource_set_binds(
    const RgResourceSetDebugInfo& resource_set,
    std::string_view resource_name
) {
    return std::ranges::find_if(
               resource_set.bindings,
               [&](const auto& binding) {
                   return binding.resource_name == resource_name;
               }
           ) != resource_set.bindings.end();
}

void require_reads_all(
    const RgPassDebugInfo& pass,
    std::initializer_list<std::string_view> texture_names
) {
    for (auto texture_name : texture_names) {
        CAPTURE(pass.name, texture_name);
        CHECK(pass_reads(pass, texture_name));
    }
}

void require_writes_all(
    const RgPassDebugInfo& pass,
    std::initializer_list<std::string_view> texture_names
) {
    for (auto texture_name : texture_names) {
        CAPTURE(pass.name, texture_name);
        CHECK(pass_writes(pass, texture_name));
    }
}

void require_resource_set_binds_all(
    const RgResourceSetDebugInfo& resource_set,
    std::initializer_list<std::string_view> resource_names
) {
    for (auto resource_name : resource_names) {
        CAPTURE(resource_set.name, resource_name);
        CHECK(resource_set_binds(resource_set, resource_name));
    }
}

std::vector<RenderGraphResourceBinding>
vxgi_mipmap_base_bindings_for_test(const VxgiGraphHandles& vxgi) {
    return {
        vxgi.radiance,
        vxgi.mipmap[0],
        vxgi.mipmap[1],
        vxgi.mipmap[2],
        vxgi.mipmap[3],
        vxgi.mipmap[4],
        vxgi.mipmap[5],
    };
}

std::vector<RenderGraphResourceBinding>
vxgi_mipmap_volume_bindings_for_test(const VxgiGraphHandles& vxgi) {
    return {
        std::shared_ptr<Buffer> {},
        std::shared_ptr<TextureView> {},
        std::shared_ptr<TextureView> {},
        std::shared_ptr<TextureView> {},
        std::shared_ptr<TextureView> {},
        std::shared_ptr<TextureView> {},
        std::shared_ptr<TextureView> {},
        vxgi.mipmap[0],
        vxgi.mipmap[1],
        vxgi.mipmap[2],
        vxgi.mipmap[3],
        vxgi.mipmap[4],
        vxgi.mipmap[5],
    };
}

void add_deferred_prepass(
    RenderGraph& graph,
    DeferredGBufferGraphHandles& gbuffer
) {
    graph.add_pass<RenderGraph::Empty>(
        "deferred_prepass",
        [&gbuffer](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            gbuffer.position_ao = builder.create_texture(
                "g_position_ao",
                make_texture_desc(TextureType::Texture2D)
            );
            gbuffer.normal_roughness = builder.create_texture(
                "g_normal_roughness",
                make_texture_desc(TextureType::Texture2D)
            );
            gbuffer.albedo_metallic = builder.create_texture(
                "g_albedo_metallic",
                make_texture_desc(TextureType::Texture2D)
            );
            gbuffer.specular = builder.create_texture(
                "g_specular",
                make_texture_desc(TextureType::Texture2D)
            );
            gbuffer.emissive_depth = builder.create_texture(
                "g_emissive_depth",
                make_texture_desc(TextureType::Texture2D)
            );
            gbuffer.depth =
                builder.create_texture("camera_depth", make_depth_desc());

            gbuffer.position_ao = builder.write_texture(
                gbuffer.position_ao,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.normal_roughness = builder.write_texture(
                gbuffer.normal_roughness,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.albedo_metallic = builder.write_texture(
                gbuffer.albedo_metallic,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.specular = builder.write_texture(
                gbuffer.specular,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.emissive_depth = builder.write_texture(
                gbuffer.emissive_depth,
                RenderGraphAccess::ColorAttachmentWrite
            );
            gbuffer.depth = builder.write_texture(
                gbuffer.depth,
                RenderGraphAccess::DepthStencilWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_shadow_passes(RenderGraph& graph, ShadowMapGraphHandles& shadows) {
    RgTextureHandle shadow_blur_intermediate;

    graph.add_pass<RenderGraph::Empty>(
        "shadow_map",
        [&shadows](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto shadow = builder.create_texture(
                "shadow_map",
                make_texture_desc(TextureType::Texture2D)
            );
            shadow = builder.write_texture(
                shadow,
                RenderGraphAccess::ColorAttachmentWrite
            );
            shadows.entries.push_back(
                ShadowMapGraphEntry {
                    .texture = nullptr,
                    .handle = shadow,
                }
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "shadow_blur_horizontal",
        [&shadows, &shadow_blur_intermediate](
            RenderGraphBuilder& builder,
            RenderGraph::Empty&
        ) {
            shadow_blur_intermediate = builder.create_texture(
                "shadow_blur_intermediate",
                make_texture_desc(TextureType::Texture2D)
            );
            shadow_blur_intermediate = builder.write_texture(
                shadow_blur_intermediate,
                RenderGraphAccess::ColorAttachmentWrite
            );
            (void)builder.create_resource_set(
                "shadow.blur.horizontal",
                std::shared_ptr<const ResourceLayout> {},
                {shadows.first()}
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "shadow_blur_vertical",
        [&shadows, shadow_blur_intermediate](
            RenderGraphBuilder& builder,
            RenderGraph::Empty&
        ) {
            auto& entry = shadows.entries.front();
            auto blurred_shadow = builder.write_texture(
                entry.handle,
                RenderGraphAccess::ColorAttachmentWrite
            );
            (void)builder.create_resource_set(
                "shadow.blur.vertical",
                std::shared_ptr<const ResourceLayout> {},
                {shadow_blur_intermediate}
            );
            builder.side_effect();
            entry.handle = blurred_shadow;
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_vxgi_voxelize_pass(RenderGraph& graph, VxgiGraphHandles& vxgi) {
    graph.add_pass<RenderGraph::Empty>(
        "vxgi_voxelize",
        [&vxgi](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            vxgi.albedo = builder.import_texture(
                "vxgi.albedo",
                std::shared_ptr<Texture> {}
            );
            vxgi.normal = builder.import_texture(
                "vxgi.normal",
                std::shared_ptr<Texture> {}
            );
            vxgi.emissive = builder.import_texture(
                "vxgi.emissive",
                std::shared_ptr<Texture> {}
            );
            vxgi.radiance = builder.import_texture(
                "vxgi.radiance",
                std::shared_ptr<Texture> {}
            );
            vxgi.static_flag = builder.import_texture(
                "vxgi.static_flag",
                std::shared_ptr<Texture> {}
            );
            for (std::size_t i = 0; i < vxgi.mipmap.size(); ++i) {
                vxgi.mipmap[i] = builder.import_texture(
                    "vxgi.mipmap." + std::to_string(i),
                    std::shared_ptr<Texture> {}
                );
            }

            (void)builder.create_resource_set(
                "vxgi.volumes",
                std::shared_ptr<const ResourceLayout> {},
                {
                    vxgi.albedo,
                    vxgi.normal,
                    vxgi.emissive,
                    vxgi.radiance,
                    vxgi.static_flag,
                }
            );
            (void)builder.create_resource_set(
                "vxgi.voxelization",
                std::shared_ptr<const ResourceLayout> {},
                {std::shared_ptr<Buffer> {}}
            );
            (void)builder.write_texture(
                builder.create_texture(
                    "vxgi.voxelization_target",
                    make_texture_desc(TextureType::Texture2D)
                ),
                RenderGraphAccess::ColorAttachmentWrite
            );

            vxgi.albedo = builder.write_texture(
                vxgi.albedo,
                RenderGraphAccess::TextureReadWrite
            );
            vxgi.normal = builder.write_texture(
                vxgi.normal,
                RenderGraphAccess::TextureReadWrite
            );
            vxgi.emissive = builder.write_texture(
                vxgi.emissive,
                RenderGraphAccess::TextureReadWrite
            );
            vxgi.static_flag = builder.write_texture(
                vxgi.static_flag,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_vxgi_inject_radiance_pass(
    RenderGraph& graph,
    VxgiGraphHandles& vxgi,
    LightingResources& lighting,
    std::shared_ptr<Buffer> voxelization_uniform_buffer
) {
    auto& blackboard = graph.blackboard();
    graph.add_pass<RenderGraph::Empty>(
        "vxgi_inject_radiance",
        [&blackboard, &vxgi, &lighting, voxelization_uniform_buffer](
            RenderGraphBuilder& builder,
            RenderGraph::Empty&
        ) {
            (void)builder.create_resource_set(
                "vxgi.volumes",
                std::shared_ptr<const ResourceLayout> {},
                {
                    vxgi.albedo,
                    vxgi.normal,
                    vxgi.emissive,
                    vxgi.radiance,
                    vxgi.static_flag,
                }
            );
            (void)builder.create_resource_set(
                "lighting",
                std::shared_ptr<const ResourceLayout> {},
                lighting_resource_bindings(
                    lighting,
                    first_shadow_map_graph_handle(blackboard),
                    nullptr
                )
            );
            (void)builder.create_resource_set(
                "vxgi.voxelization",
                std::shared_ptr<const ResourceLayout> {},
                {voxelization_uniform_buffer}
            );

            vxgi.normal = builder.write_texture(
                vxgi.normal,
                RenderGraphAccess::TextureReadWrite
            );
            vxgi.radiance = builder.write_texture(
                vxgi.radiance,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_vxgi_mipmap_base_pass(
    RenderGraph& graph,
    VxgiGraphHandles& vxgi,
    std::string name
) {
    graph.add_pass<RenderGraph::Empty>(
        std::move(name),
        [&vxgi](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            (void)builder.create_resource_set(
                "vxgi.mipmap_base",
                std::shared_ptr<const ResourceLayout> {},
                vxgi_mipmap_base_bindings_for_test(vxgi)
            );
            for (auto& mipmap : vxgi.mipmap) {
                mipmap = builder.write_texture(
                    mipmap,
                    RenderGraphAccess::TextureReadWrite
                );
            }
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_vxgi_mipmap_volume_pass(
    RenderGraph& graph,
    VxgiGraphHandles& vxgi,
    std::string name
) {
    graph.add_pass<RenderGraph::Empty>(
        std::move(name),
        [&vxgi](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            (void)builder.create_resource_set(
                "vxgi.mipmap_volume",
                std::shared_ptr<const ResourceLayout> {},
                vxgi_mipmap_volume_bindings_for_test(vxgi)
            );
            for (auto& mipmap : vxgi.mipmap) {
                mipmap = builder.write_texture(
                    mipmap,
                    RenderGraphAccess::TextureReadWrite
                );
            }
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_vxgi_inject_propagation_pass(
    RenderGraph& graph,
    VxgiGraphHandles& vxgi,
    VxgiInjectPropagation& inject_propagation
) {
    graph.add_pass<RenderGraph::Empty>(
        "vxgi_inject_propagation",
        [&vxgi, &inject_propagation](
            RenderGraphBuilder& builder,
            RenderGraph::Empty&
        ) {
            (void)builder.create_resource_set(
                "vxgi.inject_propagation",
                std::shared_ptr<const ResourceLayout> {},
                vxgi_inject_propagation_resource_bindings(
                    inject_propagation,
                    vxgi
                )
            );
            vxgi.radiance = builder.write_texture(
                vxgi.radiance,
                RenderGraphAccess::TextureReadWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_direct_lighting_pass(
    RenderGraph& graph,
    DeferredGBufferGraphHandles& gbuffer,
    DeferredLightingGraphHandles& deferred_lighting,
    LightingResources& lighting
) {
    auto& blackboard = graph.blackboard();
    graph.add_pass<RenderGraph::Empty>(
        "direct_lighting",
        [&blackboard,
         &gbuffer,
         &deferred_lighting,
         &lighting](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            (void)builder.create_resource_set(
                "deferred.gbuffer",
                std::shared_ptr<const ResourceLayout> {},
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    std::shared_ptr<Sampler> {}
                )
            );
            (void)builder.create_resource_set(
                "lighting",
                std::shared_ptr<const ResourceLayout> {},
                lighting_resource_bindings(
                    lighting,
                    first_shadow_map_graph_handle(blackboard),
                    nullptr
                )
            );
            deferred_lighting.direct = builder.create_texture(
                "direct_lighting",
                make_texture_desc(TextureType::Texture2D)
            );
            deferred_lighting.direct = builder.write_texture(
                deferred_lighting.direct,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_indirect_lighting_pass(
    RenderGraph& graph,
    DeferredGBufferGraphHandles& gbuffer,
    DeferredLightingGraphHandles& deferred_lighting,
    VxgiGraphHandles& vxgi,
    VxgiResources& vxgi_resources
) {
    graph.add_pass<RenderGraph::Empty>(
        "indirect_lighting",
        [&gbuffer,
         &deferred_lighting,
         &vxgi,
         &vxgi_resources](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            (void)builder.create_resource_set(
                "deferred.gbuffer",
                std::shared_ptr<const ResourceLayout> {},
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    std::shared_ptr<Sampler> {}
                )
            );
            (void)builder.create_resource_set(
                "vxgi",
                std::shared_ptr<const ResourceLayout> {},
                vxgi_deferred_resource_bindings(vxgi_resources, vxgi)
            );
            deferred_lighting.indirect = builder.create_texture(
                "indirect_lighting",
                make_texture_desc(TextureType::Texture2D)
            );
            deferred_lighting.indirect = builder.write_texture(
                deferred_lighting.indirect,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_composite_pass(
    RenderGraph& graph,
    DeferredGBufferGraphHandles& gbuffer,
    DeferredLightingGraphHandles& deferred_lighting
) {
    graph.add_pass<RenderGraph::Empty>(
        "composite_lighting",
        [&gbuffer,
         &deferred_lighting](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            (void)builder.create_resource_set(
                "deferred.gbuffer",
                std::shared_ptr<const ResourceLayout> {},
                deferred_gbuffer_resource_bindings(
                    gbuffer,
                    std::shared_ptr<Sampler> {}
                )
            );
            (void)builder.create_resource_set(
                "deferred.composite",
                std::shared_ptr<const ResourceLayout> {},
                deferred_composite_resource_bindings(deferred_lighting)
            );
            deferred_lighting.composite = builder.create_texture(
                "composite_lighting",
                make_texture_desc(TextureType::Texture2D)
            );
            deferred_lighting.composite = builder.write_texture(
                deferred_lighting.composite,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );
}

void add_blit_composite_pass(
    RenderGraph& graph,
    DeferredLightingGraphHandles& deferred_lighting
) {
    graph.add_pass<RenderGraph::Empty>(
        "blit_composite",
        [&deferred_lighting](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(
                deferred_lighting.composite,
                RenderGraphAccess::BlitSource
            );
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );
}

} // namespace

TEST_CASE(
    "PBR RenderGraph keeps direct lighting and VXGI dependencies split",
    "[pbr][render-graph]"
) {
    RenderGraph graph;
    auto& shadows = graph.blackboard().emplace<ShadowMapGraphHandles>();
    auto& gbuffer = graph.blackboard().emplace<DeferredGBufferGraphHandles>();
    auto& vxgi = graph.blackboard().emplace<VxgiGraphHandles>();
    auto& deferred_lighting =
        graph.blackboard().emplace<DeferredLightingGraphHandles>();
    LightingResources lighting;
    VxgiResources vxgi_resources;
    VxgiInjectPropagation inject_propagation;

    add_deferred_prepass(graph, gbuffer);
    add_shadow_passes(graph, shadows);
    add_vxgi_voxelize_pass(graph, vxgi);
    add_vxgi_inject_radiance_pass(
        graph,
        vxgi,
        lighting,
        std::shared_ptr<Buffer> {}
    );
    add_vxgi_mipmap_base_pass(graph, vxgi, "vxgi_mipmap_base");
    add_vxgi_mipmap_volume_pass(graph, vxgi, "vxgi_mipmap_volume");
    add_vxgi_inject_propagation_pass(graph, vxgi, inject_propagation);
    add_vxgi_mipmap_base_pass(
        graph,
        vxgi,
        "vxgi_mipmap_base_after_propagation"
    );
    add_vxgi_mipmap_volume_pass(
        graph,
        vxgi,
        "vxgi_mipmap_volume_after_propagation"
    );
    add_direct_lighting_pass(graph, gbuffer, deferred_lighting, lighting);
    add_indirect_lighting_pass(
        graph,
        gbuffer,
        deferred_lighting,
        vxgi,
        vxgi_resources
    );
    add_composite_pass(graph, gbuffer, deferred_lighting);
    add_blit_composite_pass(graph, deferred_lighting);

    REQUIRE(graph.compile());

    const auto& debug = graph.debug_info();
    const auto deferred_prepass = pass_index(debug, "deferred_prepass");
    const auto shadow_blur_vertical = pass_index(debug, "shadow_blur_vertical");
    const auto vxgi_voxelize = pass_index(debug, "vxgi_voxelize");
    const auto vxgi_inject_radiance = pass_index(debug, "vxgi_inject_radiance");
    const auto vxgi_mipmap_volume = pass_index(debug, "vxgi_mipmap_volume");
    const auto vxgi_inject_propagation =
        pass_index(debug, "vxgi_inject_propagation");
    const auto vxgi_mipmap_volume_after_propagation =
        pass_index(debug, "vxgi_mipmap_volume_after_propagation");
    const auto direct_lighting = pass_index(debug, "direct_lighting");
    const auto indirect_lighting = pass_index(debug, "indirect_lighting");

    const auto& voxelize = pass_named(debug, "vxgi_voxelize");
    require_writes_all(
        voxelize,
        {
            "vxgi.albedo",
            "vxgi.normal",
            "vxgi.emissive",
            "vxgi.static_flag",
        }
    );
    CHECK_FALSE(pass_writes(voxelize, "vxgi.radiance"));

    const auto& voxelize_volumes_set =
        resource_set_named(debug, "vxgi.volumes", vxgi_voxelize);
    CHECK(voxelize_volumes_set.active);
    require_resource_set_binds_all(
        voxelize_volumes_set,
        {
            "vxgi.albedo",
            "vxgi.normal",
            "vxgi.emissive",
            "vxgi.radiance",
            "vxgi.static_flag",
        }
    );

    const auto& voxelize_uniform_set =
        resource_set_named(debug, "vxgi.voxelization", vxgi_voxelize);
    CHECK(voxelize_uniform_set.active);
    CHECK(voxelize_uniform_set.bindings.size() == 1);

    const auto& inject = pass_named(debug, "vxgi_inject_radiance");
    require_reads_all(
        inject,
        {
            "shadow_map",
            "vxgi.albedo",
            "vxgi.normal",
            "vxgi.emissive",
            "vxgi.radiance",
            "vxgi.static_flag",
        }
    );
    CHECK(pass_depends_on(inject, shadow_blur_vertical));
    CHECK(pass_depends_on(inject, vxgi_voxelize));
    CHECK_FALSE(pass_depends_on(inject, deferred_prepass));
    CHECK_FALSE(pass_reads(inject, "g_position_ao"));

    const auto& voxelization_set =
        resource_set_named(debug, "vxgi.voxelization", vxgi_inject_radiance);
    CHECK(voxelization_set.active);
    CHECK(voxelization_set.bindings.size() == 1);

    const auto& mipmap_volume_set =
        resource_set_named(debug, "vxgi.mipmap_volume", vxgi_mipmap_volume);
    CHECK(mipmap_volume_set.active);
    require_resource_set_binds_all(
        mipmap_volume_set,
        {
            "vxgi.mipmap.0",
            "vxgi.mipmap.1",
            "vxgi.mipmap.2",
            "vxgi.mipmap.3",
            "vxgi.mipmap.4",
            "vxgi.mipmap.5",
        }
    );

    const auto& direct = pass_named(debug, "direct_lighting");
    require_reads_all(
        direct,
        {
            "g_position_ao",
            "g_normal_roughness",
            "g_albedo_metallic",
            "g_specular",
            "g_emissive_depth",
            "shadow_map",
        }
    );
    CHECK(pass_depends_on(direct, deferred_prepass));
    CHECK(pass_depends_on(direct, shadow_blur_vertical));
    CHECK_FALSE(pass_depends_on(direct, vxgi_voxelize));
    CHECK_FALSE(pass_depends_on(direct, vxgi_inject_radiance));
    CHECK_FALSE(pass_depends_on(direct, vxgi_mipmap_volume_after_propagation));
    CHECK_FALSE(pass_reads(direct, "vxgi.normal"));
    CHECK_FALSE(pass_reads(direct, "vxgi.radiance"));
    CHECK_FALSE(pass_reads(direct, "vxgi.mipmap.0"));

    const auto& mipmap_volume_after_set = resource_set_named(
        debug,
        "vxgi.mipmap_volume",
        vxgi_mipmap_volume_after_propagation
    );
    CHECK(mipmap_volume_after_set.active);
    require_resource_set_binds_all(
        mipmap_volume_after_set,
        {
            "vxgi.mipmap.0",
            "vxgi.mipmap.1",
            "vxgi.mipmap.2",
            "vxgi.mipmap.3",
            "vxgi.mipmap.4",
            "vxgi.mipmap.5",
        }
    );

    const auto& indirect = pass_named(debug, "indirect_lighting");
    require_reads_all(
        indirect,
        {
            "g_position_ao",
            "g_normal_roughness",
            "g_albedo_metallic",
            "g_specular",
            "g_emissive_depth",
            "vxgi.normal",
            "vxgi.radiance",
            "vxgi.mipmap.0",
            "vxgi.mipmap.1",
            "vxgi.mipmap.2",
            "vxgi.mipmap.3",
            "vxgi.mipmap.4",
            "vxgi.mipmap.5",
        }
    );
    CHECK(pass_depends_on(indirect, deferred_prepass));
    CHECK(pass_depends_on(indirect, vxgi_inject_radiance));
    CHECK(pass_depends_on(indirect, vxgi_inject_propagation));
    CHECK(pass_depends_on(indirect, vxgi_mipmap_volume_after_propagation));
    CHECK_FALSE(pass_depends_on(indirect, shadow_blur_vertical));
    CHECK_FALSE(pass_depends_on(indirect, direct_lighting));
    CHECK_FALSE(pass_reads(indirect, "shadow_map"));

    const auto& propagation_set = resource_set_named(
        debug,
        "vxgi.inject_propagation",
        vxgi_inject_propagation
    );
    CHECK(propagation_set.active);
    require_resource_set_binds_all(
        propagation_set,
        {
            "vxgi.radiance",
            "vxgi.albedo",
            "vxgi.normal",
            "vxgi.mipmap.0",
            "vxgi.mipmap.1",
            "vxgi.mipmap.2",
            "vxgi.mipmap.3",
            "vxgi.mipmap.4",
            "vxgi.mipmap.5",
        }
    );

    const auto& composite = pass_named(debug, "composite_lighting");
    require_reads_all(
        composite,
        {
            "direct_lighting",
            "indirect_lighting",
            "g_position_ao",
        }
    );
    CHECK(pass_depends_on(composite, direct_lighting));
    CHECK(pass_depends_on(composite, indirect_lighting));
}

TEST_CASE(
    "PBR blit composite pass builder skips absent MainSwapchain",
    "[pbr][render-graph]"
) {
    World world;
    world.add_resource(RenderGraph {});
    auto& graph = world.resource<RenderGraph>();
    auto& deferred_lighting =
        graph.blackboard().emplace<DeferredLightingGraphHandles>();

    graph.add_pass<RenderGraph::Empty>(
        "composite_source",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            deferred_lighting.composite = builder.create_texture(
                "composite_lighting",
                make_texture_desc(TextureType::Texture2D)
            );
            deferred_lighting.composite = builder.write_texture(
                deferred_lighting.composite,
                RenderGraphAccess::ColorAttachmentWrite
            );
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    world.run_system_once(build_blit_composite_pass);

    REQUIRE(graph.compile());
    CHECK_FALSE(has_pass_named(graph.debug_info(), "blit_composite"));
}
