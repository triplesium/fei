#include "snapshot_types.hpp"

#include "devtools/json.hpp"
#include "refl/generated.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

using namespace fei;
using namespace fei::devtools;
using namespace fei::devtools::pbr;

namespace {

class TestTexture : public Texture {
  public:
    TestTexture(PixelFormat format, uint32 width, uint32 height) :
        m_format(format), m_width(width), m_height(height) {}

    PixelFormat format() const override { return m_format; }
    uint32 width() const override { return m_width; }
    uint32 height() const override { return m_height; }
    uint32 depth() const override { return 1; }
    uint32 mip_level() const override { return 1; }
    uint32 layer() const override { return 1; }
    BitFlags<TextureUsage> usage() const override {
        return {TextureUsage::RenderTarget, TextureUsage::Sampled};
    }
    TextureType type() const override { return TextureType::Texture2D; }
    TextureSampleCount sample_count() const override {
        return TextureSampleCount::Count1;
    }

  private:
    PixelFormat m_format;
    uint32 m_width;
    uint32 m_height;
};

void register_snapshot_test_types() {
    static bool registered = false;
    if (!registered) {
        register_generated_reflection();
        registered = true;
    }
}

const RenderTargetSnapshot&
find_target(const RenderTargetsSnapshot& snapshot, const std::string& id) {
    auto iter =
        std::ranges::find(snapshot.targets, id, &RenderTargetSnapshot::id);
    REQUIRE(iter != snapshot.targets.end());
    return *iter;
}

const RenderTargetViewSnapshot&
find_view(const RenderTargetSnapshot& target, const std::string& id) {
    auto iter =
        std::ranges::find(target.views, id, &RenderTargetViewSnapshot::id);
    REQUIRE(iter != target.views.end());
    return *iter;
}

} // namespace

TEST_CASE(
    "PBR render target snapshot describes availability and capture support",
    "[devtools][pbr][snapshot]"
) {
    register_snapshot_test_types();

    DeferredViewTargets targets;
    targets.position_ao =
        std::make_shared<TestTexture>(PixelFormat::Rgba16Float, 1280, 720);
    targets.albedo_metallic =
        std::make_shared<TestTexture>(PixelFormat::Rgba8Unorm, 1280, 720);
    targets.specular =
        std::make_shared<TestTexture>(PixelFormat::Rgba8Unorm, 1280, 720);
    targets.composite =
        std::make_shared<TestTexture>(PixelFormat::Rgba8Unorm, 1280, 720);

    auto snapshot = make_render_targets_snapshot(targets);

    CHECK(snapshot.available);
    CHECK(snapshot.total_targets == 8);
    CHECK(snapshot.available_targets == 4);
    CHECK(snapshot.total_views == 11);
    CHECK(snapshot.available_views == 5);

    const auto& position = find_target(snapshot, "pbr.deferred.position_ao");
    CHECK(position.available);
    CHECK(position.format == "Rgba16Float");
    REQUIRE(position.views.size() == 1);
    const auto& position_view = find_view(position, "position");
    CHECK(position_view.available);
    CHECK(position_view.blob_capability == "pbr.gbuffer.position");
    CHECK(position_view.visualization == "position");

    const auto& albedo = find_target(snapshot, "pbr.deferred.albedo_metallic");
    CHECK(albedo.available);
    CHECK(albedo.width == 1280);
    CHECK(albedo.height == 720);
    REQUIRE(albedo.views.size() == 2);
    const auto& albedo_view = find_view(albedo, "albedo");
    CHECK(albedo_view.blob_capability == "pbr.gbuffer.albedo");
    CHECK(albedo_view.visualization == "color");
    const auto& metallic_view = find_view(albedo, "metallic");
    CHECK(metallic_view.blob_capability == "pbr.gbuffer.metallic");
    CHECK(metallic_view.visualization == "scalar_alpha");

    const auto& normal = find_target(snapshot, "pbr.deferred.normal_roughness");
    CHECK_FALSE(normal.available);
    CHECK(normal.format == "Rgba16Float");
    REQUIRE(normal.views.size() == 2);
    CHECK_FALSE(find_view(normal, "normal").available);
    CHECK_FALSE(find_view(normal, "roughness").available);

    auto json = encode_json(Ref(snapshot));
    REQUIRE(json);
    CHECK(json->find(R"("total_targets":8)") != std::string::npos);
    CHECK(json->find(R"("total_views":11)") != std::string::npos);
    CHECK(
        json->find(R"("blob_capability":"pbr.gbuffer.specular")") !=
        std::string::npos
    );
    CHECK(json->find("$type") == std::string::npos);
}
