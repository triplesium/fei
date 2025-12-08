#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "asset/id.hpp"
#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/system.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"

#include <memory>
#include <unordered_set>
#include <vector>

namespace fei {

template<typename Source, typename Target>
class RenderAssetAdapter {
  public:
    virtual Optional<Target>
    prepare_asset(const Source& source_asset, World& world) = 0;
};

template<typename T>
class RenderAssets {
  private:
    std::unordered_map<AssetId, std::unique_ptr<T>> m_render_assets;

  public:
    RenderAssets() = default;
    RenderAssets(const RenderAssets&) = delete;
    RenderAssets& operator=(const RenderAssets&) = delete;
    RenderAssets(RenderAssets&&) = default;
    RenderAssets& operator=(RenderAssets&&) = default;
    ~RenderAssets() = default;

    Optional<T&> get(AssetId id) const {
        auto it = m_render_assets.find(id);
        if (it != m_render_assets.end()) {
            return *it->second;
        }
        return nullopt;
    }
    void insert(AssetId id, std::unique_ptr<T> asset) {
        m_render_assets.emplace(id, std::move(asset));
    }
    void remove(AssetId id) { m_render_assets.erase(id); }
};

template<typename T>
struct ExtractedAssets {
    struct Entry {
        AssetId id;
        const T* asset;
    };
    std::vector<Entry> extracted;
    std::unordered_set<AssetId> removed;
    std::unordered_set<AssetId> modified;
    std::unordered_set<AssetId> added;
};

template<typename Source>
void extract_render_assets(
    WorldRef world,
    EventReader<AssetEvent<Source>> events,
    Res<Assets<Source>> assets
) {
    std::unordered_set<AssetId> need_extracting, added, removed, modified;
    for (auto event = events.next(); event; event = events.next()) {
        AssetEventType type = event->type;
        AssetId id = event->id;
        switch (type) {
            case AssetEventType::Added: {
                need_extracting.insert(id);
                break;
            }
            case AssetEventType::Modified: {
                need_extracting.insert(id);
                modified.insert(id);
                break;
            }
            case AssetEventType::Removed: {
                removed.insert(id);
                need_extracting.erase(id);
                modified.erase(id);
                break;
            }
        }
    }
    std::vector<typename ExtractedAssets<Source>::Entry> extracted;
    for (AssetId id : need_extracting) {
        if (auto source_asset = assets->get(id)) {
            extracted.push_back(typename ExtractedAssets<Source>::Entry {
                .id = id,
                .asset = source_asset
                             .transform([](auto& a) {
                                 return &a;
                             })
                             .value_or(nullptr),
            });
            added.insert(id);
        }
    }
    world->add_resource(ExtractedAssets<Source> {
        .extracted = std::move(extracted),
        .removed = std::move(removed),
        .modified = std::move(modified),
        .added = std::move(added),
    });
}

template<typename Source, typename Target, typename Adapter>
void prepare_assets(
    Res<ExtractedAssets<Source>> extracted_assets,
    Res<RenderAssets<Target>> render_assets,
    WorldRef world
) {
    for (auto id : extracted_assets->removed) {
        render_assets->remove(id);
    }
    extracted_assets->removed.clear();

    for (const auto& entry : extracted_assets->extracted) {
        auto id = entry.id;
        auto* source_asset = entry.asset;
        render_assets->remove(id);
        auto render_asset = Adapter().prepare_asset(*source_asset, *world);
        if (render_asset) {
            render_assets->insert(
                id,
                std::make_unique<Target>(std::move(*render_asset))
            );
        }
    }
    extracted_assets->extracted.clear();
}

template<typename Source, typename Target, typename Adapter>
struct RenderAssetPlugin : public Plugin {
    using RenderAssetType = Target;
    using SourceAssetType = Source;

    void setup(App& app) override {
        // app.add_resource<ExtractedAssets<Source>>();
        app.add_resource<RenderAssets<Target>>();
        app.add_system(RenderPrepare, extract_render_assets<Source>);
        app.add_system(RenderPrepare, prepare_assets<Source, Target, Adapter>);
    }
};

} // namespace fei
