#include "shader/compiler.hpp"
#include "shader_opengl/plugin.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace fei;

namespace {

std::filesystem::path
write_text_file(const std::filesystem::path& path, std::string content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    return path;
}

const ShaderResourceBinding&
require_resource(const ShaderDescription& shader, std::string_view name) {
    auto it = std::find_if(
        shader.resources.begin(),
        shader.resources.end(),
        [&](const ShaderResourceBinding& resource) {
            return resource.name == name;
        }
    );
    CAPTURE(name);
    REQUIRE(it != shader.resources.end());
    return *it;
}

} // namespace

TEST_CASE(
    "SlangLibraryShaderCompiler compiles Slang with in-process artifact "
    "generation",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
struct MaterialData
{
    float4 color;
};

layout(set = 2, binding = 0) ConstantBuffer<MaterialData> Material;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
#if ALPHA_TEST
    return Material.color + float4(float(LIGHT_COUNT) / 4.0, 0.0, 0.0, 0.0);
#else
    return Material.color;
#endif
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
        .defs = {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        },
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.stage == ShaderStages::Fragment);
    REQUIRE(output->description.path == "shader.slang");
    REQUIRE_FALSE(output->description.source.empty());
    REQUIRE_FALSE(output->description.wgsl.empty());
    REQUIRE_FALSE(output->description.spirv.empty());
    REQUIRE(
        output->description.defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
    REQUIRE(output->description.resources.size() == 1);
    REQUIRE(output->description.resources[0].name == "Material");
    REQUIRE(
        output->description.resources[0].kind == ResourceKind::UniformBuffer
    );
    REQUIRE(output->dependencies.size() == 1);
    REQUIRE(output->dependencies[0] == source_path.lexically_normal());
}

TEST_CASE(
    "SlangLibraryShaderCompiler maps ParameterBlock field resources to "
    "logical names",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-parameter-block";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
struct MaterialData
{
    float4 color;
};

struct MaterialBlock
{
    MaterialData data;
    Texture2D<float4> albedo_map;
    Texture2D<float4> normal_map;
    Texture2D<float4> metallic_map;
    Texture2D<float4> roughness_map;
    Texture2D<float4> emissive_map;
    Texture2D<float4> specular_map;
    SamplerState sampler;

    property float4 color
    {
        get { return data.color; }
    }
};

layout(set = 2) ParameterBlock<MaterialBlock> material;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return material.albedo_map.Sample(material.sampler, float2(0.5, 0.5)) *
           material.color;
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
        .target = ShaderCompileTarget::Vulkan,
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.resources.size() == 8);

    const auto& material = require_resource(output->description, "material");
    CHECK(material.kind == ResourceKind::UniformBuffer);
    CHECK(material.set == 2);
    CHECK(material.binding == 0);

    const auto& albedo = require_resource(output->description, "albedo_map");
    CHECK(albedo.kind == ResourceKind::TextureReadOnly);
    CHECK(albedo.set == 2);
    CHECK(albedo.binding == 1);

    const auto& normal = require_resource(output->description, "normal_map");
    CHECK(normal.kind == ResourceKind::TextureReadOnly);
    CHECK(normal.set == 2);
    CHECK(normal.binding == 2);

    const auto& material_sampler =
        require_resource(output->description, "sampler");
    CHECK(material_sampler.kind == ResourceKind::Sampler);
    CHECK(material_sampler.set == 2);
    CHECK(material_sampler.binding == 7);
}

TEST_CASE(
    "SlangLibraryShaderCompiler uses storage buffer names for OpenGL blocks",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-storage-buffer-names";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
layout(set = 0, binding = 0) RWStructuredBuffer<uint> first_buffer;
layout(set = 0, binding = 1) RWStructuredBuffer<uint> second_buffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    first_buffer[dispatch_thread_id.x] = second_buffer[dispatch_thread_id.x];
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Compute,
        .entry = "compute_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());

    const auto& first = require_resource(output->description, "first_buffer");
    CHECK(first.kind == ResourceKind::StorageBufferReadWrite);
    CHECK(first.backend_name == "first_buffer_block");

    const auto& second = require_resource(output->description, "second_buffer");
    CHECK(second.kind == ResourceKind::StorageBufferReadWrite);
    CHECK(second.backend_name == "second_buffer_block");

    INFO(output->description.source);
    CHECK(
        output->description.source.find("buffer first_buffer_block") !=
        std::string::npos
    );
    CHECK(
        output->description.source.find("buffer second_buffer_block") !=
        std::string::npos
    );
    CHECK(
        output->description.source.find("buffer RWStructuredBuffer") ==
        std::string::npos
    );
}

TEST_CASE(
    "SlangLibraryShaderCompiler emits write-only WGSL storage textures",
    "[rendering][shader-compiler][slang][wgsl]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-storage-texture";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
layout(set = 0, binding = 0, rgba32f) RWTexture2D<float4> output_texture;

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    output_texture[dispatch_thread_id.xy] = float4(1.0);
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Compute,
        .entry = "compute_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);
    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    INFO(output->description.wgsl);
    CHECK(
        output->description.wgsl.find(
            "texture_storage_2d<rgba32float, write>"
        ) != std::string::npos
    );
    CHECK(
        output->description.wgsl.find(
            "texture_storage_2d<rgba32float, read_write>"
        ) == std::string::npos
    );
}

TEST_CASE(
    "SlangLibraryShaderCompiler defines the WGSL target macro only for WGSL",
    "[rendering][shader-compiler][slang][wgsl]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-wgsl-target-macro";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
#if FEI_SHADER_TARGET_WGSL
    return float4(29.0, 0.0, 0.0, 1.0);
#else
    return float4(17.0, 0.0, 0.0, 1.0);
#endif
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);
    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    INFO(output->description.source);
    INFO(output->description.wgsl);
    CHECK(output->description.source.find("17") != std::string::npos);
    CHECK(output->description.source.find("29") == std::string::npos);
    CHECK(output->description.wgsl.find("29") != std::string::npos);
    CHECK(output->description.wgsl.find("17") == std::string::npos);
}

TEST_CASE(
    "SlangLibraryShaderCompiler keeps geometry shaders SPIR-V only",
    "[rendering][shader-compiler][slang][wgsl]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-geometry";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("geometry")]
[maxvertexcount(3)]
void geometry_main(
    triangle VertexOutput input[3],
    inout TriangleStream<VertexOutput> stream
)
{
    stream.Append(input[0]);
    stream.Append(input[1]);
    stream.Append(input[2]);
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Geometry,
        .entry = "geometry_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);
    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    CHECK_FALSE(output->description.spirv.empty());
    CHECK(output->description.wgsl.empty());
}

TEST_CASE(
    "SlangLibraryShaderCompiler uses sanitized imported uniform block names",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-uniform-buffer-names";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    auto module_path = root / "test" / "environment.slang";
    write_text_file(
        module_path,
        R"(
namespace test.environment {

public struct EnvironmentUniform {
    public float intensity;
};

}
)"
    );
    write_text_file(
        source_path,
        R"(
import test.environment;

using namespace test.environment;

layout(set = 0, binding = 0)
ConstantBuffer<EnvironmentUniform> EnvironmentMap;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(EnvironmentMap.intensity);
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);
    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());

    const auto& environment =
        require_resource(output->description, "EnvironmentMap");
    CHECK(environment.kind == ResourceKind::UniformBuffer);
    CHECK(environment.backend_name.find('.') == std::string::npos);
    INFO(output->description.source);
    CHECK(
        output->description.source.find(
            "uniform " + environment.backend_name
        ) != std::string::npos
    );
}

TEST_CASE(
    "SlangLibraryShaderCompiler tracks imported Slang modules",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-imports";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    auto module_path = root / "test" / "colors.slang";
    auto module_include_path = root / "test" / "detail" / "colors.slangh";
    write_text_file(
        module_include_path,
        R"(
public float4 imported_color()
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );
    write_text_file(
        module_path,
        R"(
#include "detail/colors.slangh"
)"
    );
    write_text_file(
        source_path,
        R"(
import test.colors;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return imported_color();
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
    };

    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.stage == ShaderStages::Fragment);
    REQUIRE_FALSE(output->description.source.empty());
    REQUIRE_FALSE(output->description.spirv.empty());
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            source_path.lexically_normal()
        ) != output->dependencies.end()
    );
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            module_path.lexically_normal()
        ) != output->dependencies.end()
    );
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            module_include_path.lexically_normal()
        ) != output->dependencies.end()
    );
}

TEST_CASE(
    "SlangLibraryShaderCompiler generates only the requested backend code",
    "[rendering][shader-compiler][slang][target]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-target";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
layout(set = 0, binding = 0) StructuredBuffer<uint> input_buffer;
layout(set = 0, binding = 1) RWStructuredBuffer<uint> output_buffer;
layout(set = 0, binding = 2) Texture2D<float4> input_textures[3];
layout(set = 0, binding = 5, rgba32f) RWTexture2D<float4> output_texture;
layout(set = 0, binding = 6) SamplerState input_sampler;

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint index = dispatch_thread_id.x;
    output_buffer[index] = input_buffer[index];
    output_texture[dispatch_thread_id.xy] =
        input_textures[0].SampleLevel(input_sampler, float2(0.5), 0.0);
}
)"
    );

    auto request = ShaderCompileRequest {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Compute,
        .entry = "compute_main",
        .target = ShaderCompileTarget::Vulkan,
    };
    OpenGLShaderCompiler compiler(ShaderCompileTarget::All);

    auto vulkan = compiler.compile(request);
    if (!vulkan) {
        INFO(vulkan.error().message);
        INFO(vulkan.error().diagnostics);
    }
    REQUIRE(vulkan.has_value());
    CHECK_FALSE(vulkan->description.spirv.empty());
    CHECK(vulkan->description.wgsl.empty());
    CHECK(vulkan->description.source.empty());
    CHECK(
        require_resource(vulkan->description, "input_buffer").kind ==
        ResourceKind::StorageBufferReadOnly
    );
    CHECK(
        require_resource(vulkan->description, "output_buffer").kind ==
        ResourceKind::StorageBufferReadWrite
    );
    const auto& input_textures =
        require_resource(vulkan->description, "input_textures");
    CHECK(input_textures.kind == ResourceKind::TextureReadOnly);
    CHECK(input_textures.array_size == 3);
    CHECK(
        require_resource(vulkan->description, "output_texture").kind ==
        ResourceKind::TextureReadWrite
    );
    CHECK(
        require_resource(vulkan->description, "input_sampler").kind ==
        ResourceKind::Sampler
    );

    request.target = ShaderCompileTarget::WebGpu;
    auto webgpu = compiler.compile(request);
    if (!webgpu) {
        INFO(webgpu.error().message);
        INFO(webgpu.error().diagnostics);
    }
    REQUIRE(webgpu.has_value());
    CHECK(webgpu->description.spirv.empty());
    CHECK_FALSE(webgpu->description.wgsl.empty());
    CHECK(webgpu->description.source.empty());
    CHECK(
        require_resource(webgpu->description, "input_buffer").kind ==
        ResourceKind::StorageBufferReadOnly
    );
    CHECK(
        require_resource(webgpu->description, "output_texture").kind ==
        ResourceKind::TextureReadWrite
    );
}
