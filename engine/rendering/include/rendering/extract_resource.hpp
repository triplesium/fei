#pragma once

#include "app/app.hpp"
#include "base/optional.hpp"
#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "rendering/extract.hpp"
#include "rendering/render_app.hpp"

#include <concepts>

namespace fei {

template<typename Target>
struct ExtractResource {
    using Source = Target;

    static Target extract_resource(const Source& source)
        requires std::copy_constructible<Target>
    {
        return source;
    }
};

template<typename Target>
concept ExtractableResource =
    requires(const typename ExtractResource<Target>::Source& source) {
        {
            ExtractResource<Target>::extract_resource(source)
        } -> std::same_as<Target>;
    } && std::assignable_from<Target&, Target>;

namespace detail {

template<ExtractableResource Target>
void extract_resource(
    Extract<Optional<ResRO<typename ExtractResource<Target>::Source>>> source,
    Optional<ResRW<Target>> target,
    Commands commands
) {
    const auto& source_resource = source.get();
    if (!source_resource) {
        return;
    }

    if (target) {
        if ((*source_resource).is_changed()) {
            **target =
                ExtractResource<Target>::extract_resource(**source_resource);
        }
        return;
    }

    commands.add_resource(
        ExtractResource<Target>::extract_resource(**source_resource)
    );
}

} // namespace detail

template<ExtractableResource Target>
App& add_extract_resource(App& app) {
    auto& render_app = app.sub_app<RenderApp>();
    auto& registry = render_app.resource<RenderExtractRegistry>();
    if (!registry.resource_types.insert(type_id<Target>()).second) {
        return app;
    }

    render_app.add_systems(RenderExtract, detail::extract_resource<Target>);
    return app;
}

} // namespace fei
