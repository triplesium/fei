#include "base/optional.hpp"
#include "pass.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace ets::detail::luau_compiler {
namespace {

bool position_before(const Luau::Position& lhs, const Luau::Position& rhs) {
    return lhs.line < rhs.line ||
           (lhs.line == rhs.line && lhs.column < rhs.column);
}

bool locations_overlap(const Luau::Location& lhs, const Luau::Location& rhs) {
    return position_before(lhs.begin, rhs.end) &&
           position_before(rhs.begin, lhs.end);
}

Optional<std::size_t>
source_offset(const LuauScriptSource& source, const Luau::Position& position) {
    std::size_t offset = 0;
    for (unsigned int line = 0; line < position.line; ++line) {
        const auto newline = source.content.find('\n', offset);
        if (newline == std::string::npos) {
            return nullopt;
        }
        offset = newline + 1;
    }
    const auto result = offset + static_cast<std::size_t>(position.column);
    if (result > source.content.size()) {
        return nullopt;
    }
    return result;
}

LuauScriptError patch_error(
    const LuauScriptSource& source,
    const SourcePatch& patch,
    std::string message
) {
    return LuauScriptError {
        source.name + ":" + std::to_string(patch.location.begin.line + 1) +
            ": " + std::string(patch.owner) + " pass " + std::move(message),
    };
}

} // namespace

void SourcePatchSet::add(
    Luau::Location location,
    std::string replacement,
    std::string_view owner
) {
    const auto duplicate =
        std::ranges::find_if(m_patches, [&](const SourcePatch& patch) {
            return patch.location.begin == location.begin &&
                   patch.location.end == location.end &&
                   patch.replacement == replacement;
        });
    if (duplicate == m_patches.end()) {
        m_patches.push_back(
            SourcePatch {
                .location = location,
                .replacement = std::move(replacement),
                .owner = std::string(owner),
            }
        );
    }
}

Result<std::string, LuauScriptError>
SourcePatchSet::apply(const LuauScriptSource& source) const {
    std::vector<const SourcePatch*> patches;
    patches.reserve(m_patches.size());
    for (const auto& patch : m_patches) {
        patches.push_back(&patch);
    }
    std::ranges::sort(patches, [](const auto* lhs, const auto* rhs) {
        return position_before(lhs->location.begin, rhs->location.begin);
    });
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const auto& patch = *patches[index];
        if (position_before(patch.location.end, patch.location.begin)) {
            return failure(
                patch_error(source, patch, "produced an invalid patch")
            );
        }
        if (index != 0 &&
            locations_overlap(patches[index - 1]->location, patch.location)) {
            return failure(patch_error(
                source,
                patch,
                "overlaps a patch produced by the " +
                    std::string(patches[index - 1]->owner) + " pass"
            ));
        }
    }

    std::string result {source.content};
    for (const auto* patch_pointer : patches | std::views::reverse) {
        const auto& patch = *patch_pointer;
        const auto begin = source_offset(source, patch.location.begin);
        const auto end = source_offset(source, patch.location.end);
        if (!begin || !end || *begin > *end) {
            return failure(patch_error(
                source,
                patch,
                "produced a patch outside the source"
            ));
        }
        result.replace(*begin, *end - *begin, patch.replacement);
    }
    return result;
}

} // namespace ets::detail::luau_compiler
