#include "graphics/shader_module.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace fei {
namespace {

ShaderResourceLayoutError
binding_error(const ShaderResourceBinding& binding, std::string message) {
    return ShaderResourceLayoutError {
        .message = "Shader resource set " + std::to_string(binding.set) +
                   " binding " + std::to_string(binding.binding) + ": " +
                   std::move(message),
    };
}

} // namespace

Result<std::vector<ResourceLayoutDescription>, ShaderResourceLayoutError>
reflect_resource_layouts(std::span<const ShaderDescription> shaders) {
    std::vector<ResourceLayoutDescription> layouts;
    for (const auto& shader : shaders) {
        for (const auto& binding : shader.resources) {
            if (binding.array_size == 0) {
                return failure(binding_error(binding, "array size is zero"));
            }
            if (layouts.size() <= binding.set) {
                layouts.resize(static_cast<std::size_t>(binding.set) + 1);
            }

            auto& elements = layouts[binding.set].elements;
            auto existing = std::ranges::find_if(
                elements,
                [&](const ResourceLayoutElementDescription& element) {
                    return element.binding == binding.binding;
                }
            );
            if (existing == elements.end()) {
                elements.push_back(
                    ResourceLayoutElementDescription {
                        .binding = binding.binding,
                        .name = binding.name,
                        .kind = binding.kind,
                        .stages = shader.stage,
                        .array_count = binding.array_size,
                    }
                );
                continue;
            }

            if (existing->name != binding.name) {
                return failure(binding_error(
                    binding,
                    "name conflicts with '" + existing->name + "'"
                ));
            }
            if (existing->kind != binding.kind) {
                return failure(
                    binding_error(binding, "resource kind conflicts")
                );
            }
            if (existing->array_count != binding.array_size) {
                return failure(binding_error(binding, "array size conflicts"));
            }
            existing->stages |= shader.stage;
        }
    }

    for (auto& layout : layouts) {
        std::ranges::sort(
            layout.elements,
            {},
            &ResourceLayoutElementDescription::binding
        );
    }
    return layouts;
}

} // namespace fei
