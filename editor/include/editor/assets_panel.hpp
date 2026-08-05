#pragma once

#include "asset/path.hpp"

#include <memory>

namespace fei {
class AssetDatabase;
class AssetImporterRegistry;
class AssetServer;
class Image;
class ImGuiImages;
template<typename T>
class Assets;
} // namespace fei

namespace fei::editor {

class ActivityLog;
class AssetBrowser;
struct Selection;

struct AssetsPanelContext {
    AssetBrowser& browser;
    Selection& selection;
    AssetServer& asset_server;
    const AssetImporterRegistry& importers;
    AssetDatabase& database;
    ActivityLog& activity;
    const Assets<Image>& images;
    ImGuiImages& image_textures;
};

class AssetsPanel {
  public:
    AssetsPanel();
    AssetsPanel(const AssetsPanel&) = delete;
    AssetsPanel& operator=(const AssetsPanel&) = delete;
    AssetsPanel(AssetsPanel&&) = delete;
    AssetsPanel& operator=(AssetsPanel&&) = delete;
    ~AssetsPanel();

    [[nodiscard]] bool is_open() const noexcept;
    void set_open(bool open) noexcept;

    void draw(AssetsPanelContext context);
    void draw_inspector(const AssetPath& path, AssetsPanelContext context);
    void shutdown(ImGuiImages& images) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fei::editor
