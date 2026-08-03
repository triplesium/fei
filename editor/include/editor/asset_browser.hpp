#pragma once

#include "asset/path.hpp"
#include "asset/source.hpp"
#include "base/optional.hpp"

#include <string>
#include <vector>

namespace fei {

class AssetServer;

namespace editor {

class AssetBrowser {
  public:
    explicit AssetBrowser(AssetPath root = AssetPath("project://"));

    [[nodiscard]] const AssetPath& root() const { return m_root; }

    [[nodiscard]] const AssetPath& current_directory() const {
        return m_current_directory;
    }

    [[nodiscard]] const std::vector<AssetEntry>& entries() const {
        return m_entries;
    }

    [[nodiscard]] const Optional<AssetPath>& selection() const {
        return m_selection;
    }

    [[nodiscard]] const Optional<std::string>& error() const { return m_error; }

    [[nodiscard]] bool refresh_requested() const { return m_refresh_requested; }

    [[nodiscard]] const AssetEntry* selected_entry() const;

    bool navigate_to(const AssetPath& directory);
    bool navigate_up();
    bool open(const AssetEntry& entry);
    bool select(const AssetEntry& entry);
    void clear_selection();

    void request_refresh() { m_refresh_requested = true; }
    bool refresh(const AssetServer& assets);

  private:
    AssetPath m_root;
    AssetPath m_current_directory;
    std::vector<AssetEntry> m_entries;
    Optional<AssetPath> m_selection;
    Optional<std::string> m_error;
    bool m_refresh_requested {true};

    [[nodiscard]] bool contains(const AssetPath& path) const;
};

} // namespace editor

} // namespace fei
