#include "plugin_build.hpp"

#include "ecs/dynamic/events.hpp"
#include "ecs/world.hpp"
#include "scripting/detail/script_system_loader.hpp"
#include "scripting/detail/state.hpp"

#include <algorithm>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace ets::detail {

LuauPluginBuildContext::LuauPluginBuildContext(
    World& world,
    LuauExecutionPool& execution_pool,
    std::shared_ptr<const LuauExecutionModule> execution_module,
    const LuauModuleSchema& schema,
    const LuauPluginDecl& plugin
) :
    m_world(&world), m_execution_pool(&execution_pool),
    m_execution_module(std::move(execution_module)), m_schema(&schema),
    m_plugin(&plugin) {}

Status<LuauScriptError>
LuauPluginBuildContext::dispatch(LuauPluginBuildOperation operation) {
    return std::visit(
        [&](auto&& current) -> Status<LuauScriptError> {
            using Operation = std::remove_cvref_t<decltype(current)>;
            if constexpr (std::same_as<Operation, LuauStateBuildOperation>) {
                const auto state = std::ranges::find(
                    m_plugin->states,
                    current.initial.type_id(),
                    &LuauStateDecl::type_id
                );
                LuauStateDecl schema_state;
                const LuauStateDecl* declaration = nullptr;
                if (state != m_plugin->states.end()) {
                    declaration = &*state;
                } else if (
                    const auto enumeration = std::ranges::find(
                        m_schema->enums,
                        current.initial.type_id(),
                        &LuauEnumDecl::type_id
                    );
                    enumeration != m_schema->enums.end()
                ) {
                    schema_state = LuauStateDecl {
                        .name = enumeration->name,
                        .qualified_name = enumeration->qualified_name,
                        .type_id = enumeration->type_id,
                        .values = enumeration->values,
                    };
                    declaration = &schema_state;
                }
                if (declaration == nullptr) {
                    return failure(
                        LuauScriptError {"Plugin state value does "
                                         "not belong to an exported "
                                         "script state type"}
                    );
                }
                return install_luau_state(
                    *m_world,
                    *declaration,
                    std::move(current.initial),
                    current.init_if_missing
                );
            } else if constexpr (
                std::same_as<Operation, LuauResourceBuildOperation>
            ) {
                const auto type = current.value.type_id();
                if (current.init_if_missing && m_world->has_resource(type)) {
                    return {};
                }
                m_world->add_resource(type, std::move(current.value));
                return {};
            } else if constexpr (
                std::same_as<Operation, LuauEventBuildOperation>
            ) {
                if (!m_world->has_resource<DynamicEvents>()) {
                    m_world->add_resource(DynamicEvents {});
                }
                m_world->resource<DynamicEvents>().register_type(current.type);
                return {};
            } else {
                auto installed = install_luau_script_system_functions(
                    *m_world,
                    *m_execution_pool,
                    m_execution_module,
                    m_plugin->source_name,
                    current.systems
                );
                if (!installed) {
                    return failure(std::move(installed.error()));
                }
                m_systems.insert(
                    m_systems.end(),
                    installed->begin(),
                    installed->end()
                );
                return {};
            }
        },
        std::move(operation)
    );
}

std::vector<SystemHandle> LuauPluginBuildContext::take_systems() {
    return std::move(m_systems);
}

} // namespace ets::detail
