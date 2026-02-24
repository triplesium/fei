#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "asset/server.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/shader.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

class ShaderCache {
  private:
    std::unordered_map<AssetId, std::shared_ptr<ShaderModule>> m_cache;
    AssetServer& m_asset_server;
    Assets<Shader>& m_shaders;
    GraphicsDevice& m_device;

  public:
    ShaderCache(
        AssetServer& asset_server,
        Assets<Shader>& shaders,
        GraphicsDevice& device
    ) : m_asset_server(asset_server), m_shaders(shaders), m_device(device) {}

    std::shared_ptr<ShaderModule> get(const AssetId& id) {
        auto it = m_cache.find(id);
        if (it != m_cache.end()) {
            return it->second;
        }
        auto shader_asset = m_shaders.get(id);
        if (!shader_asset) {
            fatal("ShaderCache: Shader asset '{}' not found", id);
        }
        auto shader_module =
            m_device.create_shader_module(shader_asset->description());
        m_cache[id] = shader_module;
        return shader_module;
    }

    std::shared_ptr<ShaderModule> get(Handle<Shader> handle) {
        return get(handle.id());
    }

    std::shared_ptr<ShaderModule> get(const ShaderRef& ref) {
        return get(ref.resolve(m_asset_server));
    }
};

} // namespace fei
