#include "rendering/render_graph.hpp"

#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class RecordingCommandBuffer : public CommandBuffer {
  public:
    std::vector<std::shared_ptr<const ResourceSet>> resource_sets;

    void begin() override {}
    void end() override {}

    void begin_render_pass(const RenderPassDescription&) override {}
    void end_render_pass() override {}

    void set_viewport(int32, int32, uint32, uint32) override {}
    void set_vertex_buffer(std::shared_ptr<const Buffer>) override {}
    void set_resource_set(
        uint32 index,
        std::shared_ptr<const ResourceSet> resource_set,
        std::span<const uint32>
    ) override {
        if (resource_sets.size() <= index) {
            resource_sets.resize(index + 1);
        }
        resource_sets[index] = std::move(resource_set);
    }
    void
    update_buffer(std::shared_ptr<Buffer>, const void*, std::size_t) override {}
    void draw(std::size_t, std::size_t) override {}
    void draw_indexed(std::size_t) override {}
    void dispatch(std::size_t, std::size_t, std::size_t) override {}

  protected:
    void set_render_pipeline_impl(std::shared_ptr<const Pipeline>) override {}
    void set_compute_pipeline_impl(std::shared_ptr<const Pipeline>) override {}
    void set_index_buffer_impl(
        std::shared_ptr<const Buffer>,
        IndexFormat,
        uint32
    ) override {}
    void generate_mipmaps_impl(std::shared_ptr<const Texture>) override {}
    void copy_texture_impl(
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32
    ) override {}
};

TextureDescription make_texture_desc(uint32 width = 16, uint32 height = 16) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::RenderTarget, TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
}

std::shared_ptr<Texture> make_imported_texture() {
    return std::make_shared<FakeTexture>(make_texture_desc());
}

void add_presented_texture_graph(
    RenderGraph& graph,
    TextureDescription desc = make_texture_desc()
) {
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", desc);
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "present",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(color, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );
}

struct ResourceSetPassData {
    RgResourceSetHandle resource_set;
};

void add_resource_set_graph(
    RenderGraph& graph,
    std::shared_ptr<const ResourceLayout> layout,
    std::shared_ptr<Sampler> sampler
) {
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<ResourceSetPassData>(
        "bind",
        [&](RenderGraphBuilder& builder, ResourceSetPassData& data) {
            data.resource_set = builder.create_resource_set(
                "graph.resource_set",
                std::move(layout),
                {
                    color,
                    std::move(sampler),
                }
            );
            builder.side_effect();
        },
        [](RenderGraphContext& context, const ResourceSetPassData& data) {
            context.command_buffer().set_resource_set(
                0,
                context.resource_set(data.resource_set)
            );
        }
    );
}

} // namespace

TEST_CASE(
    "RenderGraph orders passes from texture write/read dependencies",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    std::vector<int> executed;
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [&](RenderGraphContext&) {
            executed.push_back(1);
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "read",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(color, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [&](RenderGraphContext&) {
            executed.push_back(2);
        }
    );

    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order().size() == 2);
    REQUIRE(graph.compiled_order()[0] == 0);
    REQUIRE(graph.compiled_order()[1] == 1);

    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;
    graph.execute(device, command_buffer);

    REQUIRE(executed == std::vector<int> {1, 2});
    REQUIRE(device.texture_descriptions.size() == 1);
}

TEST_CASE(
    "RenderGraph allows imported textures to be read without a writer",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    auto imported_texture = make_imported_texture();
    bool executed = false;

    graph.add_pass<RenderGraph::Empty>(
        "read_imported",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture = builder.import_texture("imported", imported_texture);
            builder.read_texture(texture, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [&](RenderGraphContext& context) {
            executed =
                context.texture(RgTextureHandle {0, 0}) == imported_texture;
        }
    );

    REQUIRE(graph.compile());

    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;
    graph.execute(device, command_buffer);

    REQUIRE(executed);
    REQUIRE(device.texture_descriptions.empty());
}

TEST_CASE(
    "RenderGraph rejects transient texture reads before a writer",
    "[rendering][render-graph]"
) {
    RenderGraph graph;

    graph.add_pass<RenderGraph::Empty>(
        "bad_read",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture =
                builder.create_texture("unwritten", make_texture_desc());
            builder.read_texture(texture, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE_FALSE(graph.compile());
    REQUIRE_FALSE(graph.compile_error().empty());
}

TEST_CASE(
    "RenderGraph culls passes that do not reach a side effect",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    bool executed = false;

    graph.add_pass<RenderGraph::Empty>(
        "unused",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture =
                builder.create_texture("unused", make_texture_desc());
            (void)builder.write_texture(
                texture,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [&](RenderGraphContext&) {
            executed = true;
        }
    );

    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order().empty());
    REQUIRE(graph.stats().total_passes == 1);
    REQUIRE(graph.stats().active_passes == 0);
    REQUIRE(graph.stats().culled_passes == 1);

    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;
    graph.execute(device, command_buffer);

    REQUIRE_FALSE(executed);
    REQUIRE(device.texture_descriptions.empty());
}

TEST_CASE(
    "RenderGraph side effects preserve their dependencies",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "present",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(color, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order() == std::vector<uint32> {0, 1});
}

TEST_CASE(
    "RenderGraph write returns a new texture handle generation",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle created;
    RgTextureHandle written;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            created = builder.create_texture("color", make_texture_desc());
            written = builder.write_texture(
                created,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "read_written_version",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(written, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE(created.index == written.index);
    REQUIRE(created.generation == 0);
    REQUIRE(written.generation == 1);
    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order() == std::vector<uint32> {0, 1});
}

TEST_CASE(
    "RenderGraph schedules old texture version readers before later writes",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle first_version;
    RgTextureHandle second_version;

    graph.add_pass<RenderGraph::Empty>(
        "write_first",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            first_version =
                builder.create_texture("color", make_texture_desc());
            first_version = builder.write_texture(
                first_version,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "write_second",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            second_version = builder.write_texture(
                first_version,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "read_first",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(first_version, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "read_second",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(
                second_version,
                RenderGraphAccess::TextureRead
            );
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order() == std::vector<uint32> {0, 2, 1, 3});
}

TEST_CASE(
    "RenderGraph version ordering does not preserve culled readers",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle first_version;
    RgTextureHandle second_version;

    graph.add_pass<RenderGraph::Empty>(
        "write_first",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            first_version =
                builder.create_texture("color", make_texture_desc());
            first_version = builder.write_texture(
                first_version,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "unused_read_first",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(first_version, RenderGraphAccess::TextureRead);
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "write_second",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            second_version = builder.write_texture(
                first_version,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "read_second",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(
                second_version,
                RenderGraphAccess::TextureRead
            );
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order() == std::vector<uint32> {2, 3});
    REQUIRE_FALSE(graph.debug_info().passes[1].active);
}

TEST_CASE(
    "RenderGraph executes active passes in compiled order",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    std::vector<int> executed;
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "culled",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture =
                builder.create_texture("culled", make_texture_desc());
            (void)builder.write_texture(
                texture,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [&](RenderGraphContext&) {
            executed.push_back(0);
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "producer",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [&](RenderGraphContext&) {
            executed.push_back(1);
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "consumer",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(color, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [&](RenderGraphContext&) {
            executed.push_back(2);
        }
    );

    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;
    graph.execute(device, command_buffer);

    REQUIRE(executed == std::vector<int> {1, 2});
    REQUIRE(graph.compiled_order() == std::vector<uint32> {1, 2});
}

TEST_CASE(
    "RenderGraph exposes pass and texture debug info",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "culled",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture =
                builder.create_texture("culled", make_texture_desc());
            (void)builder.write_texture(
                texture,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "producer",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<RenderGraph::Empty>(
        "consumer",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            builder.read_texture(color, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE(graph.compile());

    const auto& debug = graph.debug_info();
    REQUIRE(debug.compiled);
    REQUIRE(debug.compile_error.empty());
    REQUIRE(debug.active_order == std::vector<uint32> {1, 2});
    REQUIRE(
        debug.active_pass_names ==
        std::vector<std::string> {"producer", "consumer"}
    );
    REQUIRE(debug.passes.size() == 3);
    REQUIRE_FALSE(debug.passes[0].active);
    REQUIRE(debug.passes[1].active);
    REQUIRE(debug.passes[2].active);
    REQUIRE(debug.passes[2].dependencies == std::vector<uint32> {1});
    REQUIRE(debug.passes[2].reads.size() == 1);
    REQUIRE(debug.passes[2].reads[0].texture_name == "color");
    REQUIRE(debug.passes[2].reads[0].access_name == "texture_read");

    REQUIRE(debug.textures.size() == 2);
    REQUIRE(debug.textures[0].name == "culled");
    REQUIRE_FALSE(debug.textures[0].active);
    REQUIRE(debug.textures[1].name == "color");
    REQUIRE(debug.textures[1].active);
    REQUIRE(debug.textures[1].format == "Rgba8Unorm");
    REQUIRE(debug.textures[1].usage == "Sampled|RenderTarget");
    REQUIRE(debug.textures[1].first_active_use == 0);
    REQUIRE(debug.textures[1].last_active_use == 1);
    REQUIRE(debug.stats.active_passes == 2);
    REQUIRE(debug.stats.culled_passes == 1);
}

TEST_CASE(
    "RenderGraph exposes debug info for compile failures",
    "[rendering][render-graph]"
) {
    RenderGraph graph;

    graph.add_pass<RenderGraph::Empty>(
        "bad_read",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            auto texture =
                builder.create_texture("unwritten", make_texture_desc());
            builder.read_texture(texture, RenderGraphAccess::TextureRead);
            builder.side_effect();
        },
        [](RenderGraphContext&) {
        }
    );

    REQUIRE_FALSE(graph.compile());

    const auto& debug = graph.debug_info();
    REQUIRE_FALSE(debug.compiled);
    REQUIRE_FALSE(debug.compile_error.empty());
    REQUIRE(debug.stats.total_passes == 1);
    REQUIRE(debug.stats.active_passes == 0);
    REQUIRE(debug.passes.size() == 1);
    REQUIRE(debug.passes[0].name == "bad_read");
    REQUIRE(debug.passes[0].reads.size() == 1);
    REQUIRE(debug.passes[0].reads[0].texture_name == "unwritten");
}

TEST_CASE(
    "RenderGraph reuses transient textures across graph clears",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;

    add_presented_texture_graph(graph);
    graph.execute(device, command_buffer);

    REQUIRE(device.texture_descriptions.size() == 1);
    REQUIRE(graph.stats().transient_texture_requests == 1);
    REQUIRE(graph.stats().transient_texture_hits == 0);
    REQUIRE(graph.stats().transient_texture_creates == 1);
    REQUIRE(graph.stats().texture_pool_size == 1);

    graph.clear();
    add_presented_texture_graph(graph);
    graph.execute(device, command_buffer);

    REQUIRE(device.texture_descriptions.size() == 1);
    REQUIRE(graph.stats().transient_texture_requests == 1);
    REQUIRE(graph.stats().transient_texture_hits == 1);
    REQUIRE(graph.stats().transient_texture_creates == 0);
    REQUIRE(graph.stats().texture_pool_size == 1);
}

TEST_CASE(
    "RenderGraph resource sets bind graph textures and reuse prepared sets",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;
    auto layout = device.create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                texture_read_only("color"),
                sampler("color_sampler"),
            }
        )
    );
    auto sampler = device.create_sampler(SamplerDescription::Point);

    add_resource_set_graph(graph, layout, sampler);
    REQUIRE(graph.compile());
    REQUIRE(graph.compiled_order() == std::vector<uint32> {0, 1});

    graph.execute(device, command_buffer);

    REQUIRE(device.resource_set_descriptions.size() == 1);
    REQUIRE(command_buffer.resource_sets.size() == 1);
    REQUIRE(command_buffer.resource_sets[0] != nullptr);
    REQUIRE(graph.debug_info().resource_sets.size() == 1);
    REQUIRE(graph.debug_info().resource_sets[0].resolved);
    REQUIRE(graph.debug_info().resource_sets[0].bindings.size() == 2);
    REQUIRE(graph.debug_info().resource_sets[0].bindings[0].kind == "texture");
    REQUIRE(
        graph.debug_info().resource_sets[0].bindings[0].resource_name == "color"
    );

    graph.clear();
    command_buffer.resource_sets.clear();
    add_resource_set_graph(graph, layout, sampler);
    graph.execute(device, command_buffer);

    REQUIRE(device.resource_set_descriptions.size() == 1);
    REQUIRE(command_buffer.resource_sets.size() == 1);
    REQUIRE(command_buffer.resource_sets[0] != nullptr);
}

TEST_CASE(
    "RenderGraph de-duplicates texture reads declared by resource sets",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    RgTextureHandle color;

    graph.add_pass<RenderGraph::Empty>(
        "write",
        [&](RenderGraphBuilder& builder, RenderGraph::Empty&) {
            color = builder.create_texture("color", make_texture_desc());
            color = builder.write_texture(
                color,
                RenderGraphAccess::ColorAttachmentWrite
            );
        },
        [](RenderGraphContext&) {
        }
    );

    graph.add_pass<ResourceSetPassData>(
        "bind",
        [&](RenderGraphBuilder& builder, ResourceSetPassData& data) {
            data.resource_set = builder.create_resource_set(
                "graph.duplicate_resource_set",
                std::shared_ptr<const ResourceLayout> {},
                {
                    color,
                    color,
                    color,
                }
            );
            (void)builder.create_resource_set(
                "graph.second_duplicate_resource_set",
                std::shared_ptr<const ResourceLayout> {},
                {
                    color,
                    color,
                }
            );
            builder.side_effect();
        },
        [](RenderGraphContext&, const ResourceSetPassData&) {
        }
    );

    REQUIRE(graph.compile());

    const auto& debug = graph.debug_info();
    REQUIRE(debug.compiled);
    REQUIRE(debug.active_order == std::vector<uint32> {0, 1});
    REQUIRE(debug.passes[1].reads.size() == 1);
    CHECK(debug.passes[1].reads[0].texture_name == "color");
    REQUIRE(debug.resource_sets.size() == 2);
    REQUIRE(debug.resource_sets[0].bindings.size() == 3);
    CHECK(debug.resource_sets[0].bindings[0].resource_name == "color");
    CHECK(debug.resource_sets[0].bindings[1].resource_name == "color");
    CHECK(debug.resource_sets[0].bindings[2].resource_name == "color");
    REQUIRE(debug.resource_sets[1].bindings.size() == 2);
    CHECK(debug.resource_sets[1].bindings[0].resource_name == "color");
    CHECK(debug.resource_sets[1].bindings[1].resource_name == "color");
}

TEST_CASE(
    "RenderGraph pooled transient textures require matching descriptions",
    "[rendering][render-graph]"
) {
    RenderGraph graph;
    FakeGraphicsDevice device;
    RecordingCommandBuffer command_buffer;

    add_presented_texture_graph(graph, make_texture_desc(16, 16));
    graph.execute(device, command_buffer);

    REQUIRE(device.texture_descriptions.size() == 1);

    graph.clear();
    add_presented_texture_graph(graph, make_texture_desc(32, 16));
    graph.execute(device, command_buffer);

    REQUIRE(device.texture_descriptions.size() == 2);
}
