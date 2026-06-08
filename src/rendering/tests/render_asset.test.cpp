#include "rendering/render_asset.hpp"

#include "asset/assets.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace fei;

namespace {

struct SourceAsset {
    int value {0};
};

struct PreparedAsset {
    int value {0};
};

class DoublingAdapter : public RenderAssetAdapter<SourceAsset, PreparedAsset> {
  public:
    Optional<PreparedAsset>
    prepare_asset(const SourceAsset& source_asset, World&) override {
        return PreparedAsset {.value = source_asset.value * 2};
    }
};

} // namespace

TEST_CASE(
    "RenderAssets insert, lookup, and remove prepared assets",
    "[rendering][render-asset]"
) {
    RenderAssets<PreparedAsset> assets;

    assets.insert(7, std::make_unique<PreparedAsset>(PreparedAsset {42}));

    auto found = assets.get(7);
    REQUIRE(found.has_value());
    REQUIRE(found->value == 42);

    assets.remove(7);
    REQUIRE_FALSE(assets.get(7).has_value());
}

TEST_CASE(
    "extract_render_assets gathers live assets referenced by asset events",
    "[rendering][render-asset]"
) {
    World world;
    Assets<SourceAsset> source_assets(nullptr);
    auto live = source_assets.add(std::make_unique<SourceAsset>(SourceAsset {5}));
    constexpr AssetId removed_id = 99;

    world.add_resource(Events<AssetEvent<SourceAsset>> {});
    world.add_resource(std::move(source_assets));

    world.run_system_once([&](EventWriter<AssetEvent<SourceAsset>> writer) {
        writer.send(AssetEvent<SourceAsset> {
            .type = AssetEventType::Added,
            .id = live.id(),
        });
        writer.send(AssetEvent<SourceAsset> {
            .type = AssetEventType::Removed,
            .id = removed_id,
        });
    });

    world.run_system_once(extract_render_assets<SourceAsset>);

    const auto& extracted = world.resource<ExtractedAssets<SourceAsset>>();
    REQUIRE(extracted.extracted.size() == 1);
    REQUIRE(extracted.extracted[0].id == live.id());
    REQUIRE(extracted.extracted[0].asset != nullptr);
    REQUIRE(extracted.extracted[0].asset->value == 5);
    REQUIRE(extracted.removed.contains(removed_id));
    REQUIRE(extracted.added.contains(live.id()));
}

TEST_CASE(
    "prepare_assets removes stale assets and replaces extracted assets",
    "[rendering][render-asset]"
) {
    constexpr AssetId prepared_id = 1;
    constexpr AssetId stale_id = 2;
    SourceAsset source {.value = 21};

    ExtractedAssets<SourceAsset> extracted;
    extracted.extracted.push_back(ExtractedAssets<SourceAsset>::Entry {
        .id = prepared_id,
        .asset = &source,
    });
    extracted.removed.insert(stale_id);

    RenderAssets<PreparedAsset> render_assets;
    render_assets.insert(
        prepared_id,
        std::make_unique<PreparedAsset>(PreparedAsset {1})
    );
    render_assets.insert(
        stale_id,
        std::make_unique<PreparedAsset>(PreparedAsset {2})
    );

    World world;
    world.add_resource(std::move(extracted));
    world.add_resource(std::move(render_assets));

    world.run_system_once(
        prepare_assets<SourceAsset, PreparedAsset, DoublingAdapter>
    );

    auto& prepared_assets = world.resource<RenderAssets<PreparedAsset>>();
    auto prepared = prepared_assets.get(prepared_id);
    REQUIRE(prepared.has_value());
    REQUIRE(prepared->value == 42);
    REQUIRE_FALSE(prepared_assets.get(stale_id).has_value());

    const auto& extracted_after =
        world.resource<ExtractedAssets<SourceAsset>>();
    REQUIRE(extracted_after.extracted.empty());
    REQUIRE(extracted_after.removed.empty());
}
