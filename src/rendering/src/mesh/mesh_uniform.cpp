#include "rendering/mesh/mesh_uniform.hpp"

#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "rendering/components.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace fei {

namespace {

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        fatal("Uniform buffer offset alignment cannot be zero");
    }
    return ((value + alignment - 1) / alignment) * alignment;
}

void ensure_mesh_uniform_buffer(
    const GraphicsDevice& device,
    MeshUniforms& mesh_uniforms,
    std::size_t required_capacity
) {
    if (required_capacity == 0 ||
        (mesh_uniforms.uniform_buffer &&
         required_capacity <= mesh_uniforms.capacity)) {
        return;
    }

    const auto capacity = std::bit_ceil(required_capacity);
    if (capacity > std::numeric_limits<uint32>::max() / mesh_uniforms.stride) {
        fatal("Mesh uniform buffer exceeds the dynamic offset address range");
    }

    mesh_uniforms.uniform_buffer = device.create_buffer(
        BufferDescription {
            .size = capacity * mesh_uniforms.stride,
            .usages = BufferUsages::Uniform,
        }
    );
    mesh_uniforms.resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = mesh_uniforms.resource_layout,
            .resources =
                {
                    std::make_shared<BufferRange>(
                        mesh_uniforms.uniform_buffer,
                        0,
                        sizeof(MeshUniform)
                    ),
                },
            .name = "mesh_uniforms",
        }
    );
    mesh_uniforms.capacity = capacity;
}

} // namespace

void prepare_mesh_uniforms(
    Query<Entity, const Mesh3d, const Transform3d> query,
    ResRO<GraphicsDevice> device,
    ResRO<RenderQueue> render_queue,
    ResRW<MeshUniforms> mesh_uniforms
) {
    if (!mesh_uniforms->resource_layout) {
        auto mesh_binding = uniform_buffer("mesh");
        mesh_binding.options.set(ResourceLayoutElementOptions::DynamicBinding);
        mesh_uniforms->resource_layout = device->create_resource_layout(
            ResourceLayoutDescription::sequencial(
                {ShaderStages::Vertex, ShaderStages::Fragment},
                {std::move(mesh_binding)}
            )
        );
    }

    if (mesh_uniforms->stride == 0) {
        mesh_uniforms->stride = align_up(
            sizeof(MeshUniform),
            device->uniform_buffer_offset_alignment()
        );
    }

    ensure_mesh_uniform_buffer(*device, *mesh_uniforms, query.size());
    mesh_uniforms->entries.clear();
    mesh_uniforms->upload_data.resize(query.size() * mesh_uniforms->stride);

    std::size_t index = 0;
    for (const auto& [entity, mesh3d, transform3d] : query) {
        (void)mesh3d;
        MeshUniform uniform {
            .world_from_local = transform3d.to_matrix(),
        };

        const auto offset = index++ * mesh_uniforms->stride;
        mesh_uniforms->entries.emplace(
            entity,
            MeshUniforms::Entry {
                .dynamic_offset = static_cast<uint32>(offset),
            }
        );
        std::memcpy(
            mesh_uniforms->upload_data.data() + offset,
            &uniform,
            sizeof(uniform)
        );
    }

    if (!mesh_uniforms->upload_data.empty()) {
        render_queue->write_buffer(
            mesh_uniforms->uniform_buffer,
            0,
            mesh_uniforms->upload_data.data(),
            mesh_uniforms->upload_data.size()
        );
    }
}

} // namespace fei
