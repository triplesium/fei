#include "gltf/gltf.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "core/image.hpp"
#include "gltf/loader.hpp"
#include "gltf/plugin.hpp"
#include "scene/plugin.hpp"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;

namespace {

constexpr std::uint32_t JSON_CHUNK_TYPE = 0x4E4F534A;
constexpr std::uint32_t BINARY_CHUNK_TYPE = 0x004E4942;

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xFF));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFF));
    bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFF));
    bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFF));
}

consteval unsigned char hex_digit(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<unsigned char>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<unsigned char>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        return static_cast<unsigned char>(character - 'A' + 10);
    }
    throw "invalid hex digit";
}

template<std::size_t Size>
consteval auto bytes_from_hex(const char (&hex)[Size]) {
    static_assert((Size - 1) % 2 == 0);

    std::array<std::byte, (Size - 1) / 2> bytes {};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = std::byte {static_cast<unsigned char>(
            (hex_digit(hex[index * 2]) << 4) | hex_digit(hex[index * 2 + 1])
        )};
    }
    return bytes;
}

constexpr auto rgba_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c63105030700000014500a15186264f"
    "0000000049454e44ae426082"
);

constexpr auto two_row_rgba_png = bytes_from_hex(
    "89504e470d0a1a0a"
    "0000000d49484452000000010000000208060000009981b627"
    "000000017352474200aece1ce9"
    "0000000467414d410000b18f0bfc6105"
    "000000097048597300000ec300000ec301c76fa864"
    "00000012494441541857631050307060084828680000082a0241b90d5f78"
    "0000000049454e44ae426082"
);

template<typename T>
void append_value(std::vector<std::byte>& bytes, T value) {
    const auto encoded = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

void append_chunk(
    std::vector<std::byte>& bytes,
    std::uint32_t type,
    std::span<const std::byte> data
) {
    append_u32(bytes, static_cast<std::uint32_t>(data.size()));
    append_u32(bytes, type);
    bytes.insert(bytes.end(), data.begin(), data.end());
}

std::vector<std::byte>
make_glb(std::string_view json, std::span<const std::byte> binary = {}) {
    std::vector<std::byte> bytes;
    append_u32(bytes, 0x46546C67);
    append_u32(bytes, 2);
    append_u32(bytes, 0);

    std::vector<std::byte> json_bytes;
    for (const char character : json) {
        json_bytes.push_back(static_cast<std::byte>(character));
    }
    while (json_bytes.size() % 4 != 0) {
        json_bytes.push_back(std::byte {' '});
    }
    append_chunk(bytes, JSON_CHUNK_TYPE, json_bytes);
    if (!binary.empty()) {
        std::vector<std::byte> binary_bytes(binary.begin(), binary.end());
        while (binary_bytes.size() % 4 != 0) {
            binary_bytes.push_back(std::byte {0});
        }
        append_chunk(bytes, BINARY_CHUNK_TYPE, binary_bytes);
    }

    const auto length = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[8 + index] =
            static_cast<std::byte>((length >> (index * 8)) & 0xFF);
    }
    return bytes;
}

AssetLoadResult<Gltf>
load_glb(std::string_view json, std::span<const std::byte> binary = {}) {
    auto bytes = make_glb(json, binary);
    Reader reader(bytes.data(), bytes.size());
    LoadContext context("model.glb");
    GltfLoader loader;
    return loader.load(reader, context);
}

std::string encode_base64(std::span<const std::byte> bytes) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto remaining = bytes.size() - offset;
        const auto first = std::to_integer<std::uint32_t>(bytes[offset]);
        const auto second =
            remaining > 1 ? std::to_integer<std::uint32_t>(bytes[offset + 1]) :
                            0;
        const auto third =
            remaining > 2 ? std::to_integer<std::uint32_t>(bytes[offset + 2]) :
                            0;
        const auto value = (first << 16U) | (second << 8U) | third;
        encoded.push_back(alphabet[(value >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(value >> 12U) & 0x3FU]);
        encoded.push_back(
            remaining > 1 ? alphabet[(value >> 6U) & 0x3FU] : '='
        );
        encoded.push_back(remaining > 2 ? alphabet[value & 0x3FU] : '=');
    }
    return encoded;
}

class GltfMemorySource : public AssetSource {
  private:
    std::vector<std::byte> m_bytes;

  public:
    explicit GltfMemorySource(std::vector<std::byte> bytes) :
        m_bytes(std::move(bytes)) {}

    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        return path == "model.glb";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        if (!exists(path)) {
            return failure("memory asset not found: " + path.generic_string());
        }
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class ExternalGltfMemorySource : public AssetSource {
  private:
    std::vector<std::byte> m_json;
    std::vector<std::byte> m_buffer;
    std::vector<std::byte> m_image;

  public:
    ExternalGltfMemorySource(
        std::string_view json,
        std::vector<std::byte> buffer,
        std::vector<std::byte> image = {}
    ) : m_buffer(std::move(buffer)), m_image(std::move(image)) {
        m_json.reserve(json.size());
        for (const auto character : json) {
            m_json.push_back(static_cast<std::byte>(character));
        }
    }

    std::string name() const override { return "external"; }

    bool exists(const std::filesystem::path& path) const override {
        const auto asset_path = path.generic_string();
        return asset_path == "models/model.gltf" ||
               asset_path == "models/data/triangle mesh.bin" ||
               (asset_path == "models/textures/pixel.png" && !m_image.empty());
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        const auto asset_path = path.generic_string();
        if (asset_path == "models/model.gltf") {
            return Reader(m_json.data(), m_json.size());
        }
        if (asset_path == "models/data/triangle mesh.bin") {
            return Reader(m_buffer.data(), m_buffer.size());
        }
        if (asset_path == "models/textures/pixel.png" && !m_image.empty()) {
            return Reader(m_image.data(), m_image.size());
        }
        return failure("memory asset not found: " + asset_path);
    }
};

} // namespace

TEST_CASE("Gltf starts as an empty asset catalog", "[gltf][asset]") {
    Gltf gltf;

    CHECK_FALSE(gltf.default_scene);
    CHECK(gltf.scenes.empty());
    CHECK(gltf.meshes.empty());
    CHECK(gltf.materials.empty());
    CHECK(gltf.textures.empty());
}

TEST_CASE("GltfPlugin registers its catalog loader", "[gltf][plugin]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();

    CHECK(app.has_plugin<ScenePlugin>());
    CHECK(app.has_plugin<ImagePlugin>());
    CHECK(app.has_resource<Assets<Gltf>>());
    CHECK(app.resource<Assets<Gltf>>().loader() != nullptr);
}

TEST_CASE("GltfLoader decodes images embedded in GLB", "[gltf][loader]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(
        R"({
            "asset":{"version":"2.0"},
            "buffers":[{"byteLength":70}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":70}],
            "images":[{"bufferView":0,"mimeType":"image/png","name":"pixel"}],
            "textures":[{"source":0}]
        })",
        rgba_png
    ));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->textures.size() == 1);
    auto image = app.resource<Assets<Image>>().get(gltf->textures[0]);
    REQUIRE(image);
    CHECK(image->width() == 1);
    CHECK(image->height() == 1);
    CHECK(image->channels() == 4);
    CHECK(
        image->texture_description().texture_format == PixelFormat::Rgba8Unorm
    );
    CHECK(image->data()[0] == 0x10);
    CHECK(image->data()[1] == 0x20);
    CHECK(image->data()[2] == 0x30);
    CHECK(image->data()[3] == 0x40);
}

TEST_CASE(
    "GltfLoader preserves glTF image row order",
    "[gltf][loader][image]"
) {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(
        R"({
            "asset":{"version":"2.0"},
            "buffers":[{"byteLength":125}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":125}],
            "images":[{"bufferView":0,"mimeType":"image/png"}],
            "textures":[{"source":0}]
        })",
        two_row_rgba_png
    ));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->textures.size() == 1);
    auto image = app.resource<Assets<Image>>().get(gltf->textures[0]);
    REQUIRE(image);
    REQUIRE(image->width() == 1);
    REQUIRE(image->height() == 2);
    REQUIRE(image->channels() == 4);
    CHECK(image->data()[0] == 0x10);
    CHECK(image->data()[1] == 0x20);
    CHECK(image->data()[2] == 0x30);
    CHECK(image->data()[3] == 0x40);
    CHECK(image->data()[4] == 0x50);
    CHECK(image->data()[5] == 0x60);
    CHECK(image->data()[6] == 0x70);
    CHECK(image->data()[7] == 0x80);
}

TEST_CASE("GltfLoader converts scenes and node transforms", "[gltf][loader]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(R"({
            "asset":{"version":"2.0"},
            "scene":1,
            "scenes":[{"nodes":[0]},{"nodes":[1]}],
            "nodes":[
                {"name":"isolated"},
                {"name":"root","translation":[1,2,3],"children":[2]},
                {"name":"child","matrix":[
                    2,0,0,0,
                    0,3,0,0,
                    0,0,4,0,
                    5,6,7,1
                ]}
            ]
        })"));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);

    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->default_scene);
    CHECK(*gltf->default_scene == 1);
    REQUIRE(gltf->scenes.size() == 2);

    const auto& scenes = app.resource<Assets<Scene>>();
    auto first_scene = scenes.get(gltf->scenes[0]);
    REQUIRE(first_scene);
    REQUIRE(first_scene->roots.size() == 1);
    REQUIRE(first_scene->nodes.size() == 1);
    CHECK(first_scene->nodes[0].name == "isolated");

    auto default_scene = scenes.get(gltf->scenes[1]);
    REQUIRE(default_scene);
    REQUIRE(default_scene->roots.size() == 1);
    CHECK(default_scene->roots[0] == 0);
    REQUIRE(default_scene->nodes.size() == 2);
    CHECK(default_scene->nodes[0].name == "root");
    CHECK(
        default_scene->nodes[0].local_transform.position ==
        Vector3 {1.0f, 2.0f, 3.0f}
    );
    REQUIRE(default_scene->nodes[0].children.size() == 1);
    CHECK(default_scene->nodes[0].children[0] == 1);
    CHECK(default_scene->nodes[1].name == "child");
    CHECK(
        default_scene->nodes[1].local_transform.position ==
        Vector3 {5.0f, 6.0f, 7.0f}
    );
    CHECK(
        default_scene->nodes[1].local_transform.scale ==
        Vector3 {2.0f, 3.0f, 4.0f}
    );

    auto owner = app.world().entity();
    app.world().add_component(
        owner,
        SceneSpawner {.scene = gltf->scenes[*gltf->default_scene]}
    );
    app.world().sort_systems();
    app.run_schedule(Update);

    REQUIRE(app.world().has_component<SceneInstance>(owner));
    const auto& instance = app.world().get_component<SceneInstance>(owner);
    REQUIRE(instance.node_entities.size() == 2);
    CHECK(*app.world().parent(instance.node_entities[0]) == instance.root);
    CHECK(
        *app.world().parent(instance.node_entities[1]) ==
        instance.node_entities[0]
    );
    CHECK(
        app.world()
            .get_component<SceneNodeName>(instance.node_entities[1])
            .value == "child"
    );
}

TEST_CASE(
    "GltfLoader supports empty scenes without a default",
    "[gltf][loader]"
) {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<GltfMemorySource>(
        make_glb(R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[]}]})")
    );

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    CHECK_FALSE(gltf->default_scene);
    REQUIRE(gltf->scenes.size() == 1);
    auto scene = app.resource<Assets<Scene>>().get(gltf->scenes[0]);
    REQUIRE(scene);
    CHECK(scene->nodes.empty());
    CHECK(scene->roots.empty());
}

TEST_CASE(
    "GltfLoader converts an indexed triangle primitive",
    "[gltf][loader]"
) {
    std::vector<std::byte> binary;
    constexpr std::array positions {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    constexpr std::array normals {
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    constexpr std::array tangents {
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    constexpr std::array texcoords {
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        for (std::size_t component = 0; component < 3; ++component) {
            append_value(binary, positions[(vertex * 3) + component]);
        }
        for (std::size_t component = 0; component < 3; ++component) {
            append_value(binary, normals[(vertex * 3) + component]);
        }
    }
    for (const float value : tangents) {
        append_value(binary, value);
    }
    for (const float value : texcoords) {
        append_value(
            binary,
            static_cast<std::uint16_t>(
                value * std::numeric_limits<std::uint16_t>::max()
            )
        );
    }
    for (const float value : texcoords) {
        append_value(binary, 1.0f - value);
    }
    for (const auto value : {
             std::uint8_t {255},
             std::uint8_t {0},
             std::uint8_t {0},
             std::uint8_t {255},
             std::uint8_t {0},
             std::uint8_t {255},
             std::uint8_t {0},
             std::uint8_t {0},
             std::uint8_t {0},
             std::uint8_t {0},
             std::uint8_t {255},
             std::uint8_t {255},
         }) {
        append_value(binary, value);
    }
    append_value(binary, std::uint16_t {0});
    append_value(binary, std::uint16_t {1});
    append_value(binary, std::uint16_t {2});
    REQUIRE(binary.size() == 174);

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(
        R"({
            "asset":{"version":"2.0"},
            "scene":0,
            "scenes":[{"nodes":[0]}],
            "nodes":[{"name":"triangle-node","mesh":0}],
            "buffers":[{"byteLength":174}],
            "bufferViews":[
                {"buffer":0,"byteOffset":0,"byteLength":72,"byteStride":24},
                {"buffer":0,"byteOffset":72,"byteLength":48},
                {"buffer":0,"byteOffset":120,"byteLength":12},
                {"buffer":0,"byteOffset":132,"byteLength":24},
                {"buffer":0,"byteOffset":156,"byteLength":12},
                {"buffer":0,"byteOffset":168,"byteLength":6}
            ],
            "accessors":[
                {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
                {"bufferView":1,"componentType":5126,"count":3,"type":"VEC4"},
                {"bufferView":2,"componentType":5123,"normalized":true,"count":3,"type":"VEC2"},
                {"bufferView":3,"componentType":5126,"count":3,"type":"VEC2"},
                {"bufferView":4,"componentType":5121,"normalized":true,"count":3,"type":"VEC4"},
                {"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"}
            ],
            "materials":[{
                "pbrMetallicRoughness":{
                    "baseColorFactor":[0.25,0.5,0.75,1.0],
                    "metallicFactor":0.2,
                    "roughnessFactor":0.8
                },
                "emissiveFactor":[0.1,0.2,0.3],
                "doubleSided":true
            }],
            "meshes":[{
                "name":"triangle",
                "primitives":[{
                    "attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"TEXCOORD_0":3,"TEXCOORD_1":4,"COLOR_0":5},
                    "indices":6,
                    "material":0
                }]
            }]
        })",
        binary
    ));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->meshes.size() == 1);
    REQUIRE(gltf->materials.size() == 1);
    REQUIRE(gltf->scenes.size() == 1);

    auto scene_mesh = app.resource<Assets<SceneMesh>>().get(gltf->meshes[0]);
    REQUIRE(scene_mesh);
    CHECK(scene_mesh->name == "triangle");
    REQUIRE(scene_mesh->primitives.size() == 1);
    CHECK(scene_mesh->primitives[0].material.id() == gltf->materials[0].id());

    auto material =
        app.resource<Assets<StandardMaterial>>().get(gltf->materials[0]);
    REQUIRE(material);
    CHECK(material->albedo.r == 0.25f);
    CHECK(material->albedo.g == 0.5f);
    CHECK(material->albedo.b == 0.75f);
    CHECK(material->metallic == 0.2f);
    CHECK(material->roughness == 0.8f);
    CHECK(material->emissive.r == 0.1f);
    CHECK(material->emissive.g == 0.2f);
    CHECK(material->emissive.b == 0.3f);
    CHECK(material->cull_mode == CullMode::None);

    auto mesh =
        app.resource<Assets<Mesh>>().get(scene_mesh->primitives[0].mesh);
    REQUIRE(mesh);
    CHECK(mesh->primitive() == RenderPrimitive::Triangles);
    CHECK(mesh->vertex_count() == 3);
    CHECK(mesh->index_buffer_size() == 3 * sizeof(std::uint32_t));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_NORMAL.id));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_TANGENT.id));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_UV_0.id));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_UV_1.id));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_COLOR.id));
    auto loaded_positions =
        mesh->get_attribute(Mesh::ATTRIBUTE_POSITION.id).as_float3();
    REQUIRE(loaded_positions);
    CHECK((*loaded_positions)[1] == std::array<float, 3> {1.0f, 0.0f, 0.0f});
    const auto* loaded_texcoords = static_cast<const float*>(
        mesh->get_attribute(Mesh::ATTRIBUTE_UV_0.id).data()
    );
    CHECK(loaded_texcoords[2] == 1.0f);
    CHECK(loaded_texcoords[5] == 1.0f);
    const auto* loaded_texcoords_1 = static_cast<const float*>(
        mesh->get_attribute(Mesh::ATTRIBUTE_UV_1.id).data()
    );
    CHECK(loaded_texcoords_1[0] == 1.0f);
    CHECK(loaded_texcoords_1[5] == 0.0f);
    const auto* loaded_colors = static_cast<const float*>(
        mesh->get_attribute(Mesh::ATTRIBUTE_COLOR.id).data()
    );
    CHECK(loaded_colors[0] == 1.0f);
    CHECK(loaded_colors[1] == 0.0f);
    CHECK(loaded_colors[7] == 0.0f);
    CHECK(loaded_colors[10] == 1.0f);

    auto scene = app.resource<Assets<Scene>>().get(gltf->scenes[0]);
    REQUIRE(scene);
    REQUIRE(scene->nodes.size() == 1);
    REQUIRE(scene->nodes[0].mesh);
    CHECK(scene->nodes[0].mesh->id() == gltf->meshes[0].id());
}

TEST_CASE(
    "GltfLoader reads external buffers and images relative to the glTF asset",
    "[gltf][loader]"
) {
    std::vector<std::byte> binary;
    for (const float value : {
             0.0f,
             0.0f,
             0.0f,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             1.0f,
             0.0f,
         }) {
        append_value(binary, value);
    }

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<ExternalGltfMemorySource>(
        R"({
            "asset":{"version":"2.0"},
            "scene":0,
            "scenes":[{"nodes":[0]}],
            "nodes":[{"mesh":0}],
            "buffers":[{
                "uri":"data/triangle%20mesh.bin",
                "byteLength":36
            }],
            "bufferViews":[{"buffer":0,"byteLength":36}],
            "accessors":[{
                "bufferView":0,
                "componentType":5126,
                "count":3,
                "type":"VEC3"
            }],
            "images":[{"uri":"textures/pixel.png"}],
            "textures":[{"source":0}],
            "materials":[{"pbrMetallicRoughness":{
                "baseColorTexture":{"index":0}
            }}],
            "meshes":[{"primitives":[{
                "attributes":{"POSITION":0},
                "material":0
            }]}]
        })",
        std::move(binary),
        std::vector<std::byte>(rgba_png.begin(), rgba_png.end())
    );

    auto handle = asset_server.load<Gltf>("external://models/model.gltf");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto& gltf_assets = app.resource<Assets<Gltf>>();
    auto gltf = gltf_assets.get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->meshes.size() == 1);
    REQUIRE(gltf->textures.size() == 1);
    auto scene_mesh = app.resource<Assets<SceneMesh>>().get(gltf->meshes[0]);
    REQUIRE(scene_mesh);
    REQUIRE(scene_mesh->primitives.size() == 1);
    auto mesh =
        app.resource<Assets<Mesh>>().get(scene_mesh->primitives[0].mesh);
    REQUIRE(mesh);
    CHECK(mesh->vertex_count() == 3);
    auto image = app.resource<Assets<Image>>().get(gltf->textures[0]);
    REQUIRE(image);
    CHECK(image->width() == 1);
    CHECK(image->height() == 1);
    CHECK(
        image->texture_description().texture_format ==
        PixelFormat::Rgba8UnormSrgb
    );
    CHECK(image->data()[0] == 0x10);
    CHECK(image->data()[1] == 0x20);
    CHECK(image->data()[2] == 0x30);
    CHECK(image->data()[3] == 0x40);

    auto dependencies = gltf_assets.loader_dependencies(handle);
    REQUIRE(dependencies);
    REQUIRE(dependencies->size() == 2);
    CHECK(
        (*dependencies)[0] ==
        AssetPath("external://models/data/triangle mesh.bin")
    );
    CHECK(
        (*dependencies)[1] == AssetPath("external://models/textures/pixel.png")
    );
}

TEST_CASE("GltfLoader decodes data URI buffers and images", "[gltf][loader]") {
    std::vector<std::byte> binary;
    for (const float value : {
             0.0f,
             0.0f,
             0.0f,
             1.0f,
             0.0f,
             0.0f,
             0.0f,
             1.0f,
             0.0f,
         }) {
        append_value(binary, value);
    }
    const auto buffer_uri =
        "data:application/octet-stream;base64," + encode_base64(binary);
    const auto image_uri =
        "data:image/png;base64," + encode_base64(std::span(rgba_png));
    const auto json = std::string(R"({
            "asset":{"version":"2.0"},
            "scene":0,
            "scenes":[{"nodes":[0]}],
            "nodes":[{"mesh":0}],
            "buffers":[{"uri":")") +
                      buffer_uri + R"(","byteLength":36}],
            "bufferViews":[{"buffer":0,"byteLength":36}],
            "accessors":[{
                "bufferView":0,
                "componentType":5126,
                "count":3,
                "type":"VEC3"
            }],
            "images":[{"uri":")" +
                      image_uri + R"("}],
            "textures":[{"source":0}],
            "materials":[{"pbrMetallicRoughness":{
                "baseColorTexture":{"index":0}
            }}],
            "meshes":[{"primitives":[{
                "attributes":{"POSITION":0},
                "material":0
            }]}]
        })";

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<ExternalGltfMemorySource>(
        json,
        std::vector<std::byte> {}
    );

    auto handle = asset_server.load<Gltf>("external://models/model.gltf");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto& gltf_assets = app.resource<Assets<Gltf>>();
    auto gltf = gltf_assets.get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->meshes.size() == 1);
    REQUIRE(gltf->textures.size() == 1);
    auto scene_mesh = app.resource<Assets<SceneMesh>>().get(gltf->meshes[0]);
    REQUIRE(scene_mesh);
    REQUIRE(scene_mesh->primitives.size() == 1);
    auto mesh =
        app.resource<Assets<Mesh>>().get(scene_mesh->primitives[0].mesh);
    REQUIRE(mesh);
    CHECK(mesh->vertex_count() == 3);
    auto image = app.resource<Assets<Image>>().get(gltf->textures[0]);
    REQUIRE(image);
    CHECK(image->width() == 1);
    CHECK(image->height() == 1);
    CHECK(
        image->texture_description().texture_format ==
        PixelFormat::Rgba8UnormSrgb
    );
    auto dependencies = gltf_assets.loader_dependencies(handle);
    REQUIRE(dependencies);
    CHECK(dependencies->empty());
}

TEST_CASE(
    "GltfLoader rejects data URIs with invalid image data",
    "[gltf][loader]"
) {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<ExternalGltfMemorySource>(
        R"({
            "asset":{"version":"2.0"},
            "images":[{"uri":"data:image/png;base64,AAAA"}],
            "textures":[{"source":0}]
        })",
        std::vector<std::byte> {}
    );

    auto handle = asset_server.load<Gltf>("external://models/model.gltf");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Failed);
    auto error = asset_server.load_error(handle);
    REQUIRE(error);
    CHECK(error->path == AssetPath("external://models/model.gltf"));
}

TEST_CASE("GltfLoader applies sparse position accessors", "[gltf][loader]") {
    std::vector<std::byte> binary {
        std::byte {0},
        std::byte {1},
        std::byte {2},
        std::byte {0},
    };
    constexpr std::array positions {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
    };
    for (const float value : positions) {
        append_value(binary, value);
    }
    REQUIRE(binary.size() == 40);

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(
        R"({
            "asset":{"version":"2.0"},
            "scene":0,
            "scenes":[{"nodes":[0]}],
            "nodes":[{"mesh":0}],
            "buffers":[{"byteLength":40}],
            "bufferViews":[
                {"buffer":0,"byteOffset":0,"byteLength":3},
                {"buffer":0,"byteOffset":4,"byteLength":36}
            ],
            "accessors":[{
                "componentType":5126,
                "count":3,
                "type":"VEC3",
                "sparse":{
                    "count":3,
                    "indices":{"bufferView":0,"componentType":5121},
                    "values":{"bufferView":1}
                }
            }],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
        })",
        binary
    ));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->meshes.size() == 1);
    auto scene_mesh = app.resource<Assets<SceneMesh>>().get(gltf->meshes[0]);
    REQUIRE(scene_mesh);
    REQUIRE(scene_mesh->primitives.size() == 1);
    CHECK(gltf->materials.empty());
    auto default_material = app.resource<Assets<StandardMaterial>>().get(
        scene_mesh->primitives[0].material
    );
    REQUIRE(default_material);
    CHECK(default_material->metallic == 1.0f);
    CHECK(default_material->roughness == 1.0f);
    auto mesh =
        app.resource<Assets<Mesh>>().get(scene_mesh->primitives[0].mesh);
    REQUIRE(mesh);
    CHECK(mesh->vertex_count() == 3);
    CHECK(mesh->index_buffer_size() == 3 * sizeof(std::uint32_t));
    CHECK(mesh->has_attribute(Mesh::ATTRIBUTE_NORMAL.id));
    auto loaded_positions =
        mesh->get_attribute(Mesh::ATTRIBUTE_POSITION.id).as_float3();
    REQUIRE(loaded_positions);
    CHECK((*loaded_positions)[2] == std::array<float, 3> {0.0f, 1.0f, 0.0f});
}

TEST_CASE("GltfLoader rejects malformed GLB data", "[gltf][loader]") {
    constexpr std::array<std::byte, 4> bytes {};
    Reader reader(bytes.data(), bytes.size());
    LoadContext context("model.glb");
    GltfLoader loader;

    auto result = loader.load(reader, context);

    REQUIRE_FALSE(result);
    CHECK(
        result.error().message ==
        "Failed to parse glTF asset: The file data is invalid, or the file "
        "type could not be determined."
    );
}

TEST_CASE("GltfLoader rejects invalid glTF semantics", "[gltf][loader]") {
    auto bytes = make_glb(R"({})");
    Reader reader(bytes.data(), bytes.size());
    LoadContext context("model.glb");
    GltfLoader loader;

    auto result = loader.load(reader, context);

    REQUIRE_FALSE(result);
    CHECK(
        result.error().message == "Failed to parse glTF asset: The glTF asset "
                                  "object is missing or invalid."
    );
}

TEST_CASE("GltfLoader rejects primitives without positions", "[gltf][loader]") {
    auto bytes = make_glb(R"({
        "asset":{"version":"2.0"},
        "meshes":[{"primitives":[{"attributes":{}}]}]
    })");
    Reader reader(bytes.data(), bytes.size());
    LoadContext context("model.glb");
    GltfLoader loader;

    auto result = loader.load(reader, context);

    REQUIRE_FALSE(result);
    CHECK(
        result.error().message == "glTF mesh 0 primitive 0 is missing POSITION"
    );
}

TEST_CASE(
    "GltfLoader validates accessor storage before decoding",
    "[gltf][loader][accessor]"
) {
    SECTION("bufferView stays inside its declared buffer") {
        constexpr std::array<std::byte, 16> binary {};
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":16}],
                "bufferViews":[{"buffer":0,"byteOffset":8,"byteLength":12}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"VEC3"}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(result.error().path == AssetPath("model.glb"));
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 bufferView 0 "
            "exceeds buffer 0 bounds"
        );
    }

    SECTION("accessor range stays inside its bufferView") {
        constexpr std::array<std::byte, 32> binary {};
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":32}],
                "bufferViews":[{"buffer":0,"byteLength":32}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 data exceeds its "
            "bufferView"
        );
    }

    SECTION("vertex stride cannot be smaller than an element") {
        constexpr std::array<std::byte, 36> binary {};
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":36}],
                "bufferViews":[{"buffer":0,"byteLength":36,"byteStride":8}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 bufferView 0 has an "
            "invalid byteStride"
        );
    }

    SECTION("decoded allocation is bounded") {
        auto result = load_glb(R"({
            "asset":{"version":"2.0"},
            "accessors":[{"componentType":5126,"count":30000000,"type":"VEC3"}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
        })");

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 decoded data exceeds "
            "the 256 MiB limit"
        );
    }
}

TEST_CASE(
    "GltfLoader validates accessor formats and indices",
    "[gltf][loader][accessor]"
) {
    SECTION("POSITION requires float components") {
        constexpr std::array<std::byte, 18> binary {};
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":18}],
                "bufferViews":[{"buffer":0,"byteLength":18}],
                "accessors":[{"bufferView":0,"componentType":5123,"normalized":true,"count":3,"type":"VEC3"}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 must use "
            "non-normalized float components"
        );
    }

    SECTION("indices stay inside the vertex range") {
        std::vector<std::byte> binary(36);
        binary.push_back(std::byte {0});
        binary.push_back(std::byte {1});
        binary.push_back(std::byte {3});
        binary.push_back(std::byte {0});
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":40}],
                "bufferViews":[
                    {"buffer":0,"byteLength":36},
                    {"buffer":0,"byteOffset":36,"byteLength":3}
                ],
                "accessors":[
                    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                    {"bufferView":1,"componentType":5121,"count":3,"type":"SCALAR"}
                ],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 contains an out-of-range index"
        );
    }

    SECTION("sparse indices stay inside the accessor range") {
        std::vector<std::byte> binary {
            std::byte {3},
            std::byte {0},
            std::byte {0},
            std::byte {0},
        };
        binary.resize(16);
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":16}],
                "bufferViews":[
                    {"buffer":0,"byteLength":1},
                    {"buffer":0,"byteOffset":4,"byteLength":12}
                ],
                "accessors":[{
                    "componentType":5126,
                    "count":3,
                    "type":"VEC3",
                    "sparse":{
                        "count":1,
                        "indices":{"bufferView":0,"componentType":5121},
                        "values":{"bufferView":1}
                    }
                }],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 contains an "
            "out-of-range sparse index"
        );
    }

    SECTION("sparse indices are strictly increasing") {
        std::vector<std::byte> binary {
            std::byte {1},
            std::byte {1},
            std::byte {0},
            std::byte {0},
        };
        binary.resize(28);
        auto result = load_glb(
            R"({
                "asset":{"version":"2.0"},
                "buffers":[{"byteLength":28}],
                "bufferViews":[
                    {"buffer":0,"byteLength":2},
                    {"buffer":0,"byteOffset":4,"byteLength":24}
                ],
                "accessors":[{
                    "componentType":5126,
                    "count":3,
                    "type":"VEC3",
                    "sparse":{
                        "count":2,
                        "indices":{"bufferView":0,"componentType":5121},
                        "values":{"bufferView":1}
                    }
                }],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]
            })",
            binary
        );

        REQUIRE_FALSE(result);
        CHECK(
            result.error().message ==
            "glTF mesh 0 primitive 0 POSITION accessor 0 sparse indices are "
            "not strictly increasing"
        );
    }
}

TEST_CASE(
    "GltfLoader maps material texture slots and samplers",
    "[gltf][loader]"
) {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(
        R"({
            "asset":{"version":"2.0"},
            "buffers":[{"byteLength":70}],
            "bufferViews":[{"buffer":0,"byteLength":70}],
            "images":[{"bufferView":0,"mimeType":"image/png"}],
            "samplers":[{
                "magFilter":9728,
                "minFilter":9987,
                "wrapS":33071,
                "wrapT":33648
            }],
            "textures":[
                {"source":0,"sampler":0},
                {"source":0,"sampler":0}
            ],
            "materials":[{
                "pbrMetallicRoughness":{
                    "baseColorTexture":{"index":0,"texCoord":1},
                    "metallicRoughnessTexture":{"index":1,"texCoord":0}
                },
                "normalTexture":{"index":1,"texCoord":1,"scale":0.5},
                "occlusionTexture":{"index":1,"texCoord":0,"strength":0.25},
                "emissiveTexture":{"index":0,"texCoord":1}
            }]
        })",
        rgba_png
    ));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->materials.size() == 1);
    auto material =
        app.resource<Assets<StandardMaterial>>().get(gltf->materials[0]);
    REQUIRE(material);
    REQUIRE(material->albedo_texture);
    REQUIRE(material->normal_texture);
    REQUIRE(material->metallic_roughness_texture);
    REQUIRE(material->occlusion_texture);
    REQUIRE(material->emissive_texture);
    REQUIRE(gltf->textures.size() == 2);
    CHECK(material->albedo_texture->id() == gltf->textures[0].id());
    CHECK(material->normal_texture->id() == gltf->textures[1].id());
    CHECK(material->normal_scale == 0.5f);
    CHECK(material->occlusion_strength == 0.25f);
    CHECK(material->albedo_channel == UvChannel::Uv1);
    CHECK(material->normal_channel == UvChannel::Uv1);
    CHECK(material->metallic_roughness_channel == UvChannel::Uv0);
    CHECK(material->occlusion_channel == UvChannel::Uv0);
    CHECK(material->emissive_channel == UvChannel::Uv1);
    auto albedo_image =
        app.resource<Assets<Image>>().get(*material->albedo_texture);
    REQUIRE(albedo_image);
    CHECK(
        albedo_image->texture_description().texture_format ==
        PixelFormat::Rgba8UnormSrgb
    );
    const auto& sampler = albedo_image->sampler_description();
    CHECK(sampler.mag_filter == SamplerFilter::Nearest);
    CHECK(sampler.min_filter == SamplerFilter::Linear);
    CHECK(sampler.mipmap_filter == SamplerFilter::Linear);
    CHECK(sampler.address_mode_u == SamplerAddressMode::ClampToEdge);
    CHECK(sampler.address_mode_v == SamplerAddressMode::MirrorRepeat);
}

TEST_CASE("GltfLoader maps alpha material modes", "[gltf][loader]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<GltfPlugin>();
    app.finish();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<StandardMaterial>();
    asset_server.emplace_source<GltfMemorySource>(make_glb(R"({
        "asset":{"version":"2.0"},
        "materials":[
            {
                "alphaMode":"MASK",
                "alphaCutoff":0.25,
                "pbrMetallicRoughness":{
                    "baseColorFactor":[1.0,0.5,0.25,0.75]
                }
            },
            {
                "alphaMode":"BLEND",
                "pbrMetallicRoughness":{
                    "baseColorFactor":[0.25,0.5,1.0,0.4]
                }
            }
        ]
    })"));

    auto handle = asset_server.load<Gltf>("memory://model.glb");

    REQUIRE(asset_server.load_state(handle));
    CHECK(*asset_server.load_state(handle) == AssetLoadState::Loaded);
    auto gltf = app.resource<Assets<Gltf>>().get(handle);
    REQUIRE(gltf);
    REQUIRE(gltf->materials.size() == 2);
    auto mask =
        app.resource<Assets<StandardMaterial>>().get(gltf->materials[0]);
    auto blend =
        app.resource<Assets<StandardMaterial>>().get(gltf->materials[1]);
    REQUIRE(mask);
    REQUIRE(blend);
    CHECK(mask->alpha_mode == MaterialAlphaMode::Mask);
    CHECK(mask->albedo_alpha == 0.75f);
    CHECK(mask->alpha_cutoff == 0.25f);
    CHECK(blend->alpha_mode == MaterialAlphaMode::Blend);
    CHECK(blend->albedo_alpha == 0.4f);
}
