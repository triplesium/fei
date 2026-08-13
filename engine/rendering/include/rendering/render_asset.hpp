#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "ecs/world.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"

#include <memory>
#include <unordered_set>
#include <vector>

namespace fei {

template<typename Source, typename Target>
class RenderAssetAdapter {
  public:
    virtual ~RenderAssetAdapter() = default;
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

    Optional<T&> get(AssetId id) {
        auto it = m_render_assets.find(id);
        if (it != m_render_assets.end()) {
            return *it->second;
        }
        return nullopt;
    }
    Optional<const T&> get(AssetId id) const {
        auto it = m_render_assets.find(id);
        if (it != m_render_assets.end()) {
            return *it->second;
        }
        return nullopt;
    }
    template<typename U>
    Optional<T&> get(Handle<U> handle) {
        return get(handle.id());
    }
    template<typename U>
    Optional<const T&> get(Handle<U> handle) const {
        return get(handle.id());
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
        std::shared_ptr<const T> asset;
    };
    std::vector<Entry> extracted;
    std::unordered_set<AssetId> removed;
    std::unordered_set<AssetId> modified;
    std::unordered_set<AssetId> added;
    bool initialized {false};

    Optional<const T&> get(AssetId id) const {
        for (const auto& entry : extracted) {
            if (entry.id == id && entry.asset) {
                return *entry.asset;
            }
        }
        return nullopt;
    }

    Optional<const T&> get(const Handle<T>& handle) const {
        return get(handle.id());
    }
};

template<typename Source>
void extract_render_assets(
    Extract<Optional<EventReaderRO<AssetEvent<Source>>>> events,
    Extract<Optional<ResRO<Assets<Source>>>> assets,
    ResRW<ExtractedAssets<Source>> extracted_assets
) {
    const auto& source_assets = assets.get();
    if (!source_assets) {
        return;
    }

    std::unordered_set<AssetId> need_extracting;
    auto removed = std::move(extracted_assets->removed);
    auto modified = std::move(extracted_assets->modified);
    auto added = std::move(extracted_assets->added);
    for (const auto& entry : extracted_assets->extracted) {
        if ((*source_assets)->get(entry.id)) {
            need_extracting.insert(entry.id);
        }
    }
    if (!extracted_assets->initialized) {
        for (const auto id : (*source_assets)->loaded_ids()) {
            need_extracting.insert(id);
        }
    }

    auto& source_events = events.get();
    if (source_events) {
        while (auto event = source_events->next()) {
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
                case AssetEventType::Failed: {
                    need_extracting.erase(id);
                    modified.erase(id);
                    break;
                }
            }
        }
    }
    std::vector<typename ExtractedAssets<Source>::Entry> extracted;
    for (AssetId id : need_extracting) {
        if (auto source_asset = (*source_assets)->snapshot(id)) {
            extracted.push_back(
                typename ExtractedAssets<Source>::Entry {
                    .id = id,
                    .asset = std::move(source_asset),
                }
            );
            added.insert(id);
        }
    }
    *extracted_assets = ExtractedAssets<Source> {
        .extracted = std::move(extracted),
        .removed = std::move(removed),
        .modified = std::move(modified),
        .added = std::move(added),
        .initialized = true,
    };
}

template<typename Source, typename Target, typename Adapter>
void prepare_assets(
    ResRW<ExtractedAssets<Source>> extracted_assets,
    ResRW<RenderAssets<Target>> render_assets,
    WorldRef world
) {
    for (auto id : extracted_assets->removed) {
        render_assets->remove(id);
    }
    extracted_assets->removed.clear();
    extracted_assets->modified.clear();
    extracted_assets->added.clear();

    std::vector<typename ExtractedAssets<Source>::Entry> pending;
    for (const auto& entry : extracted_assets->extracted) {
        auto id = entry.id;
        auto render_asset = Adapter().prepare_asset(*entry.asset, *world);
        if (!render_asset) {
            pending.push_back(entry);
            continue;
        }
        render_assets->remove(id);
        render_assets->insert(
            id,
            std::make_unique<Target>(std::move(*render_asset))
        );
    }
    extracted_assets->extracted = std::move(pending);
}

template<typename Source, typename Target, typename Adapter>
struct RenderAssetPlugin : public Plugin {
    using RenderAssetType = Target;
    using SourceAssetType = Source;

    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<RenderingCorePlugin>();
    }

    void setup(App& app) override {
        app.sub_app<RenderApp>()
            .add_resource<ExtractedAssets<Source>>()
            .template add_resource<RenderAssets<Target>>()
            .add_systems(
                RenderExtract,
                FEI_SYSTEM_NAME(
                    "extract_render_assets",
                    (extract_render_assets<Source>)
                )
            )
            .add_systems(
                RenderUpdate,
                FEI_SYSTEM_NAME(
                    "prepare_render_assets",
                    (prepare_assets<Source, Target, Adapter>)
                ) | in_set<RenderingSystems::PrepareAssets>()
            );
    }
};

} // namespace fei
