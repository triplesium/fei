#pragma once
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "base/log.hpp"
#include "graphics/enums.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>

namespace fei {

struct Shader {
    std::filesystem::path path;
    std::string source;
    ShaderStages stage;
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
        } else {
            fei::error("Unknown shader extension: {}", path.string());
            return nullptr;
        }
        std::string source = reader.as_string();

        return std::make_unique<Shader>(Shader {path, source, stage});
    }
};

} // namespace fei
