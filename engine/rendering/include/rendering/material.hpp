#pragma once
#include "asset/plugin.hpp"
#include "base/hash.hpp"
#include "ecs/world.hpp"
#include "graphics/enums.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"

#include <concepts>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

enum class MaterialShaderType : uint8 {
    Vertex,
    Fragment,
    PrepassVertex,
    PrepassFragment,
};

enum class MaterialAlphaMode : uint8 {
    Opaque,
    Mask,
    Blend,
    Additive,
};

struct MaterialPipelineState {
    MaterialAlphaMode alpha_mode {MaterialAlphaMode::Opaque};
    CullMode cull_mode {CullMode::Back};
    bool depth_write {true};

    bool operator==(const MaterialPipelineState&) const = default;
};

} // namespace fei

MAKE_STD_HASHABLE(
    fei::MaterialPipelineState,
    alpha_mode,
    cull_mode,
    depth_write
)

namespace fei {

class Material {
  public:
    virtual ~Material() = default;
    virtual ShaderRef vertex_shader() const {
        return ShaderRef::default_shader();
    }
    virtual ShaderRef fragment_shader() const {
        return ShaderRef::default_shader();
    }
    virtual ShaderRef prepass_vertex_shader() const {
        return ShaderRef::default_shader();
    }
    virtual ShaderRef prepass_fragment_shader() const {
        return ShaderRef::default_shader();
    }
    virtual ShaderDefs shader_defs(MaterialShaderType /*type*/) const {
        return {};
    }
    virtual MaterialPipelineState pipeline_state() const { return {}; }

    virtual std::shared_ptr<ResourceLayout> create_resource_layout(
        const GraphicsDevice& device,
        const RenderingDefaults& /*defaults*/,
        const RenderAssets<GpuImage>& /*gpu_images*/
    ) const {
        auto elements = resource_layout_elements();
        if (elements.empty()) {
            return nullptr;
        }
        return device.create_resource_layout(
            ResourceLayoutDescription {
                .elements = elements,
            }
        );
    }

    virtual std::shared_ptr<ResourceSet> create_resource_set(
        const GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const {
        auto layout = create_resource_layout(device, defaults, gpu_images);
        if (!layout) {
            return nullptr;
        }
        auto resources = this->resources(device, defaults, gpu_images);
        return device.create_resource_set(
            ResourceSetDescription {
                .layout = layout,
                .resources = resources,
                .name = "material",
            }
        );
    }

    virtual std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const = 0;

    virtual std::vector<std::shared_ptr<const BindableResource>> resources(
        const GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const = 0;

    virtual bool
    resources_ready(const RenderAssets<GpuImage>& /*gpu_images*/) const {
        return true;
    }

    // Used for caching prepared materials and pipelines. Materials with the
    // same hash value will share the same resource layout.
    virtual std::size_t hash() const = 0;
};

struct PreparedMaterialShader {
    ShaderRef ref;
    ShaderDefs defs;
};

class PreparedMaterial {
  private:
    std::unordered_map<MaterialShaderType, PreparedMaterialShader> m_shaders;
    std::shared_ptr<ResourceLayout> m_layout;
    std::shared_ptr<ResourceSet> m_resource_set;
    MaterialPipelineState m_pipeline_state;
    std::size_t m_hash;

  public:
    PreparedMaterial(
        std::unordered_map<MaterialShaderType, PreparedMaterialShader> shaders,
        std::shared_ptr<ResourceLayout> layout,
        std::shared_ptr<ResourceSet> resource_set,
        std::size_t hash,
        MaterialPipelineState pipeline_state = {}
    ) :
        m_shaders(std::move(shaders)), m_layout(std::move(layout)),
        m_resource_set(std::move(resource_set)),
        m_pipeline_state(pipeline_state), m_hash(hash) {}

    Optional<PreparedMaterialShader&> shader_request(MaterialShaderType type) {
        auto it = m_shaders.find(type);
        if (it != m_shaders.end()) {
            return it->second;
        }
        return nullopt;
    }
    Optional<const PreparedMaterialShader&>
    shader_request(MaterialShaderType type) const {
        auto it = m_shaders.find(type);
        if (it != m_shaders.end()) {
            return it->second;
        }
        return nullopt;
    }
    std::shared_ptr<ResourceLayout> resource_layout() { return m_layout; }
    std::shared_ptr<const ResourceLayout> resource_layout() const {
        return m_layout;
    }
    std::shared_ptr<ResourceSet> resource_set() { return m_resource_set; }
    std::shared_ptr<const ResourceSet> resource_set() const {
        return m_resource_set;
    }
    const MaterialPipelineState& pipeline_state() const {
        return m_pipeline_state;
    }
    std::size_t hash() const { return m_hash; }
};

template<std::derived_from<Material> SourceMaterial>
class MaterialAdapter
    : public RenderAssetAdapter<SourceMaterial, PreparedMaterial> {
  public:
    Optional<PreparedMaterial>
    prepare_asset(const SourceMaterial& source_asset, World& world) override {
        auto& device = world.resource<GraphicsDevice>();
        auto& rendering_defaults = world.resource<RenderingDefaults>();
        auto& gpu_images = world.resource<RenderAssets<GpuImage>>();

        if (!source_asset.resources_ready(gpu_images)) {
            return nullopt;
        }

        auto layout = source_asset.create_resource_layout(
            device,
            rendering_defaults,
            gpu_images
        );
        if (!layout) {
            return nullopt;
        }

        auto resource_set = device.create_resource_set(
            ResourceSetDescription {
                .layout = layout,
                .resources =
                    source_asset
                        .resources(device, rendering_defaults, gpu_images),
                .name = "material",
            }
        );
        if (!resource_set) {
            return nullopt;
        }
        auto prepared_hash = source_asset.hash();
        auto pipeline_state = source_asset.pipeline_state();
        hash_combine(prepared_hash, pipeline_state);
        std::unordered_map<MaterialShaderType, PreparedMaterialShader> shaders;
        auto load_shader = [&](MaterialShaderType type, const ShaderRef& ref) {
            if (ref.is_default()) {
                return;
            }
            auto defs = source_asset.shader_defs(type);
            defs = normalized_shader_defs(std::move(defs));
            hash_combine(prepared_hash, static_cast<int>(type));
            hash_combine(prepared_hash, ref.hash());
            hash_combine(prepared_hash, defs);
            shaders[type] = PreparedMaterialShader {
                .ref = ref,
                .defs = std::move(defs),
            };
        };
        load_shader(MaterialShaderType::Vertex, source_asset.vertex_shader());
        load_shader(
            MaterialShaderType::Fragment,
            source_asset.fragment_shader()
        );
        load_shader(
            MaterialShaderType::PrepassVertex,
            source_asset.prepass_vertex_shader()
        );
        load_shader(
            MaterialShaderType::PrepassFragment,
            source_asset.prepass_fragment_shader()
        );
        return PreparedMaterial {
            std::move(shaders),
            std::move(layout),
            std::move(resource_set),
            prepared_hash,
            pipeline_state
        };
    }
};

template<std::derived_from<Material> M>
class MaterialPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugins(
            AssetPlugin<M> {},
            RenderAssetPlugin<M, PreparedMaterial, MaterialAdapter<M>> {}
        );
    }
};

} // namespace fei
