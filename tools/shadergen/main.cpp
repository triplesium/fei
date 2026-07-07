#include "shadergen/shadergen.hpp"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path glsl;
    std::filesystem::path reflect;
    std::filesystem::path slang_reflect;
};

void configure_options(CLI::App& app, Options& options) {
    app.add_option("--input", options.input, "Input SPIR-V file")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("--glsl", options.glsl, "Output OpenGL GLSL file")
        ->required();
    app.add_option("--reflect", options.reflect, "Output reflection JSON file")
        ->required();
    app.add_option(
        "--slang-reflect",
        options.slang_reflect,
        "Optional Slang reflection JSON file for logical resource names"
    );
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    CLI::App app {"Generate shader backend artifacts"};
    configure_options(app, options);
    CLI11_PARSE(app, argc, argv);

    try {
        fei::shadergen::generate_shader_artifacts(
            fei::shadergen::ShaderArtifactGenerationRequest {
                .spirv_path = options.input,
                .opengl_path = options.glsl,
                .reflection_path = options.reflect,
                .slang_reflection_path = options.slang_reflect,
            }
        );
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fei-shadergen: " << e.what() << '\n';
        return 1;
    }
}
