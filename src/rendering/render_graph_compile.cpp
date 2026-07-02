#include "rendering/render_graph.hpp"

#include <algorithm>
#include <deque>
#include <format>

namespace fei {

namespace {

std::string handle_name(RgTextureHandle handle) {
    return std::format("{}:{}", handle.index, handle.generation);
}

void add_edge(
    std::vector<std::vector<uint32>>& outgoing,
    std::vector<std::vector<uint32>>& incoming,
    uint32 from,
    uint32 to
) {
    if (from == to) {
        return;
    }
    auto& edges = outgoing[from];
    if (std::ranges::find(edges, to) == edges.end()) {
        edges.push_back(to);
        incoming[to].push_back(from);
    }
}

} // namespace

bool RenderGraph::compile() {
    m_compile_error.clear();
    m_compiled_order.clear();
    m_compiled = false;

    const auto pass_count = m_passes.size();
    std::vector<std::vector<uint32>> data_outgoing(pass_count);
    std::vector<std::vector<uint32>> data_incoming(pass_count);
    m_stats.total_passes = pass_count;
    m_stats.active_passes = 0;
    m_stats.culled_passes = pass_count;
    m_stats.transient_texture_requests = 0;
    m_stats.transient_texture_hits = 0;
    m_stats.transient_texture_creates = 0;
    update_texture_pool_stats();

    for (auto& pass : m_passes) {
        pass->active = false;
    }

    auto fail = [&](std::string error) {
        m_compile_error = std::move(error);
        update_debug_info(data_incoming);
        return false;
    };

    for (uint32 pass_index = 0; pass_index < pass_count; ++pass_index) {
        auto& pass = *m_passes[pass_index];

        for (const auto& read : pass.reads) {
            if (!is_valid(read.handle)) {
                return fail(
                    std::format(
                        "Pass '{}' reads invalid texture handle {}",
                        pass.name,
                        handle_name(read.handle)
                    )
                );
            }

            const auto& resource = texture_resource(read.handle);
            const auto& version = resource.versions[read.handle.generation];
            if (version.writer_pass < 0) {
                if (!resource.imported) {
                    return fail(
                        std::format(
                            "Pass '{}' reads transient texture '{}' before it "
                            "is written",
                            pass.name,
                            resource.name
                        )
                    );
                }
                continue;
            }
            add_edge(
                data_outgoing,
                data_incoming,
                static_cast<uint32>(version.writer_pass),
                pass_index
            );
        }

        for (const auto& write : pass.writes) {
            if (!is_valid(write.handle)) {
                return fail(
                    std::format(
                        "Pass '{}' writes invalid texture handle {}",
                        pass.name,
                        handle_name(write.handle)
                    )
                );
            }
        }
    }

    std::vector<bool> active(pass_count, false);
    std::vector<uint32> stack;
    for (uint32 pass_index = 0; pass_index < pass_count; ++pass_index) {
        if (m_passes[pass_index]->side_effect) {
            active[pass_index] = true;
            stack.push_back(pass_index);
        }
    }

    while (!stack.empty()) {
        const auto pass_index = stack.back();
        stack.pop_back();
        for (auto dependency : data_incoming[pass_index]) {
            if (!active[dependency]) {
                active[dependency] = true;
                stack.push_back(dependency);
            }
        }
    }

    auto outgoing = data_outgoing;
    auto incoming = data_incoming;
    for (uint32 pass_index = 0; pass_index < pass_count; ++pass_index) {
        if (!active[pass_index]) {
            continue;
        }

        for (const auto& read : m_passes[pass_index]->reads) {
            const auto& resource = texture_resource(read.handle);
            const auto version_count =
                static_cast<uint32>(resource.versions.size());
            for (uint32 generation = read.handle.generation + 1;
                 generation < version_count;
                 ++generation) {
                const auto writer_pass =
                    resource.versions[generation].writer_pass;
                if (writer_pass >= 0 && active[writer_pass]) {
                    add_edge(
                        outgoing,
                        incoming,
                        pass_index,
                        static_cast<uint32>(writer_pass)
                    );
                }
            }
        }
    }

    std::vector<uint32> indegree(pass_count, 0);
    uint32 active_count = 0;
    for (uint32 pass_index = 0; pass_index < pass_count; ++pass_index) {
        if (!active[pass_index]) {
            continue;
        }
        ++active_count;
        for (auto dependency : incoming[pass_index]) {
            if (active[dependency]) {
                ++indegree[pass_index];
            }
        }
    }

    std::deque<uint32> ready;
    for (uint32 pass_index = 0; pass_index < pass_count; ++pass_index) {
        if (active[pass_index] && indegree[pass_index] == 0) {
            ready.push_back(pass_index);
        }
    }

    while (!ready.empty()) {
        auto pass_index = ready.front();
        ready.pop_front();
        m_compiled_order.push_back(pass_index);

        for (auto dependent : outgoing[pass_index]) {
            if (!active[dependent]) {
                continue;
            }
            --indegree[dependent];
            if (indegree[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }

    if (m_compiled_order.size() != active_count) {
        return fail("Render graph contains a dependency cycle");
    }

    m_stats.active_passes = active_count;
    m_stats.culled_passes = pass_count - active_count;
    update_texture_pool_stats();

    for (auto& texture : m_textures) {
        texture.first_active_use = RgTextureHandle::InvalidIndex;
        texture.last_active_use = RgTextureHandle::InvalidIndex;
    }

    for (uint32 use_index = 0; use_index < m_compiled_order.size();
         ++use_index) {
        const auto pass_index = m_compiled_order[use_index];
        const auto& pass = *m_passes[pass_index];
        auto track_texture_use = [&](const TextureUse& texture_use) {
            auto& texture = texture_resource(texture_use.handle);
            if (texture.first_active_use == RgTextureHandle::InvalidIndex) {
                texture.first_active_use = use_index;
            }
            texture.last_active_use = use_index;
        };

        for (const auto& read : pass.reads) {
            track_texture_use(read);
        }
        for (const auto& write : pass.writes) {
            track_texture_use(write);
        }
    }

    for (auto pass_index : m_compiled_order) {
        m_passes[pass_index]->active = true;
    }
    m_compiled = true;
    update_debug_info(incoming);
    return true;
}

} // namespace fei
