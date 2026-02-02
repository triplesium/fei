#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "graphics/enums.hpp"
#include "graphics/shader_module.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <variant>

namespace fei {

struct Shader {
    std::filesystem::path path;
    std::string source;
    ShaderStages stage;

    ShaderDescription description() const {
        return ShaderDescription {.stage = stage, .source = source};
    }
};

class ShaderLoader : public AssetLoader<Shader> {
  public:
    std::expected<std::unique_ptr<Shader>, std::error_code>
    load(Reader& reader, const LoadContext& context) override {
        auto path = context.asset_path().path();
        ShaderStages stage;
        if (path.extension() == ".vert") {
            stage = ShaderStages::Vertex;
        } else if (path.extension() == ".frag") {
            stage = ShaderStages::Fragment;
        } else if (path.extension() == ".comp") {
            stage = ShaderStages::Compute;
        } else {
            fei::error("Unknown shader extension: {}", path.string());
            return nullptr;
        }
        std::string source = reader.as_string();

        return std::make_unique<Shader>(Shader {path, source, stage});
    }
};

class ShaderRef {
  private:
    std::variant<Handle<Shader>, AssetPath> source;

  public:
    ShaderRef(Handle<Shader> handle) : source(handle) {}
    ShaderRef(const AssetPath& path) : source(path) {}
    ShaderRef(const char* path) : source(AssetPath(path)) {}

    Handle<Shader> resolve(AssetServer& asset_server) const {
        if (std::holds_alternative<Handle<Shader>>(source)) {
            return std::get<Handle<Shader>>(source);
        } else {
            return asset_server.load<Shader>(std::get<AssetPath>(source));
        }
    }
};

} // namespace fei
