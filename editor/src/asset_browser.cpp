#include "editor/asset_browser.hpp"

#include "asset/server.hpp"

#include <algorithm>
#include <utility>

namespace fei::editor {

AssetBrowser::AssetBrowser(AssetPath root) :
    m_root(root.normalized()), m_current_directory(m_root) {}

const AssetEntry* AssetBrowser::selected_entry() const {
    if (!m_selection) {
        return nullptr;
    }
    const auto entry =
        std::ranges::find(m_entries, *m_selection, &AssetEntry::path);
    return entry == m_entries.end() ? nullptr : &*entry;
}

bool AssetBrowser::contains(const AssetPath& path) const {
    const auto normalized = path.normalized();
    if (normalized.source() != m_root.source() || normalized.is_unapproved()) {
        return false;
    }
    if (m_root.path().empty()) {
        return true;
    }

    const auto relative = normalized.path().lexically_relative(m_root.path());
    if (relative.empty()) {
        return normalized.path() == m_root.path();
    }
    return !relative.is_absolute() && *relative.begin() != "..";
}

bool AssetBrowser::navigate_to(const AssetPath& directory) {
    auto normalized = directory.normalized();
    if (!normalized.source() && m_root.source()) {
        normalized = normalized.with_source(*m_root.source());
    }
    if (!contains(normalized) || normalized == m_current_directory) {
        return false;
    }

    m_current_directory = std::move(normalized);
    clear_selection();
    request_refresh();
    return true;
}

bool AssetBrowser::navigate_up() {
    if (m_current_directory == m_root) {
        return false;
    }

    auto parent = AssetPath(m_current_directory.path().parent_path());
    if (m_current_directory.source()) {
        parent = parent.with_source(*m_current_directory.source());
    }
    if (!contains(parent)) {
        parent = m_root;
    }
    return navigate_to(parent);
}

bool AssetBrowser::open(const AssetEntry& entry) {
    return entry.kind == AssetEntryKind::Directory && navigate_to(entry.path);
}

bool AssetBrowser::select(const AssetEntry& entry) {
    if (m_selection && *m_selection == entry.path) {
        return false;
    }
    m_selection = entry.path;
    return true;
}

void AssetBrowser::clear_selection() {
    m_selection = nullopt;
}

bool AssetBrowser::refresh(const AssetServer& assets) {
    m_refresh_requested = false;
    auto entries = assets.list(m_current_directory, false);
    if (!entries) {
        m_entries.clear();
        clear_selection();
        m_error = std::move(entries.error());
        return false;
    }

    m_entries = std::move(*entries);
    std::ranges::sort(
        m_entries,
        [](const AssetEntry& lhs, const AssetEntry& rhs) {
            if (lhs.kind != rhs.kind) {
                return lhs.kind == AssetEntryKind::Directory;
            }
            return lhs.path.path().filename().string() <
                   rhs.path.path().filename().string();
        }
    );
    m_error = nullopt;
    if (m_selection && !selected_entry()) {
        clear_selection();
    }
    return true;
}

} // namespace fei::editor
