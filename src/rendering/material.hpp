#pragma once
#include "asset/plugin.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_cache.hpp"

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
    DeferredVertex,
    DeferredFragment,
};

class Material {
  public:
    virtual ~Material() = default;
    virtual ShaderRef vertex_shader() const = 0;
    virtual ShaderRef fragment_shader() const = 0;
    virtual ShaderRef prepass_vertex_shader() const = 0;
    virtual ShaderRef prepass_fragment_shader() const = 0;
    virtual ShaderRef deferred_vertex_shader() const = 0;
    virtual ShaderRef deferred_fragment_shader() const = 0;

    virtual std::shared_ptr<ResourceLayout> create_resource_layout(
        GraphicsDevice& device,
        const RenderingDefaults& /*defaults*/,
        const RenderAssets<GpuImage>& /*gpu_images*/
    ) const {
        auto elements = resource_layout_elements();
        if (elements.empty()) {
            return nullptr;
        }
        return device.create_resource_layout(ResourceLayoutDescription {
            .elements = elements,
        });
    }

    virtual std::shared_ptr<ResourceSet> create_resource_set(
        GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const {
        auto layout = create_resource_layout(device, defaults, gpu_images);
        if (!layout) {
            return nullptr;
        }
        auto resources = this->resources(device, defaults, gpu_images);
        return device.create_resource_set(ResourceSetDescription {
            .layout = layout,
            .resources = resources,
        });
    }

    virtual std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const = 0;

    virtual std::vector<std::shared_ptr<BindableResource>> resources(
        GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const = 0;

    // Used for caching prepared materials and pipelines. Materials with the
    // same hash value will share the same resource layout.
    virtual std::size_t hash() const = 0;
};

class PreparedMaterial {
  private:
    std::unordered_map<MaterialShaderType, std::shared_ptr<ShaderModule>>
        m_shaders;
    std::shared_ptr<ResourceLayout> m_layout;
    std::shared_ptr<ResourceSet> m_resource_set;
    std::size_t m_hash;

  public:
    PreparedMaterial(
        std::unordered_map<MaterialShaderType, std::shared_ptr<ShaderModule>>
            shaders,
        std::shared_ptr<ResourceLayout> layout,
        std::shared_ptr<ResourceSet> resource_set,
        std::size_t hash
    ) :
        m_shaders(std::move(shaders)), m_layout(std::move(layout)),
        m_resource_set(std::move(resource_set)), m_hash(hash) {}

    std::shared_ptr<ShaderModule> shader(MaterialShaderType type) const {
        auto it = m_shaders.find(type);
        if (it != m_shaders.end()) {
            return it->second;
        }
        return nullptr;
    }
    std::shared_ptr<ResourceLayout> resource_layout() const { return m_layout; }
    std::shared_ptr<ResourceSet> resource_set() const { return m_resource_set; }
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
        auto& shader_cache = world.resource<ShaderCache>();

        auto layout = source_asset.create_resource_layout(
            device,
            rendering_defaults,
            gpu_images
        );
        if (!layout) {
            return nullopt;
        }

        auto resource_set = device.create_resource_set(ResourceSetDescription {
            .layout = layout,
            .resources =
                source_asset.resources(device, rendering_defaults, gpu_images),
        });
        if (!resource_set) {
            return nullopt;
        }
        std::unordered_map<MaterialShaderType, std::shared_ptr<ShaderModule>>
            shader_modules;
        auto load_shader = [&](MaterialShaderType type, const ShaderRef& ref) {
            shader_modules[type] = shader_cache.get(ref);
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
        load_shader(
            MaterialShaderType::DeferredVertex,
            source_asset.deferred_vertex_shader()
        );
        load_shader(
            MaterialShaderType::DeferredFragment,
            source_asset.deferred_fragment_shader()
        );
        return PreparedMaterial {
            std::move(shader_modules),
            std::move(layout),
            std::move(resource_set),
            source_asset.hash()
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
