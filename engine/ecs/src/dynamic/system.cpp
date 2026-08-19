#include "ecs/dynamic/system.hpp"

#include "base/log.hpp"
#include "ecs/world.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace fei {
namespace {

Result<std::vector<Ref>, DynamicSystemError> prepare_dynamic_params(
    std::string_view name,
    DynamicSystemParams& params,
    World& world,
    SystemTicks system_ticks,
    std::vector<DynamicSystemParam*>& prepared_params
) {
    std::vector<Ref> args;
    args.reserve(params.size());
    prepared_params.reserve(params.size());
    for (auto& param : params) {
        if (!param) {
            return failure(
                DynamicSystemError {
                    "Dynamic callable '" + std::string(name) +
                    "' has null param"
                }
            );
        }

        auto arg = param->prepare(world, system_ticks);
        if (!arg) {
            return failure(
                DynamicSystemError {
                    "Dynamic callable '" + std::string(name) +
                    "' failed to prepare param: " + arg.error().message
                }
            );
        }
        args.push_back(*arg);
        prepared_params.push_back(param.get());
    }
    return args;
}

void finish_dynamic_params(std::vector<DynamicSystemParam*>& params) {
    for (auto* param : params) {
        param->finish();
    }
    params.clear();
}

} // namespace

DynamicSystem::DynamicSystem(
    std::string name,
    DynamicSystemParams params,
    std::unique_ptr<DynamicSystemExecutor> executor
) :
    m_name(std::move(name)), m_params(std::move(params)),
    m_executor(std::move(executor)),
    m_access(dynamic_system_access_for_params(m_params)) {}

void DynamicSystem::execute(World& world, SystemTicks system_ticks) {
    std::vector<DynamicSystemParam*> prepared_params;
    auto args = prepare_dynamic_params(
        m_name,
        m_params,
        world,
        system_ticks,
        prepared_params
    );
    if (!args) {
        error("{}", args.error().message);
        finish_dynamic_params(prepared_params);
        return;
    }

    if (!m_executor) {
        error("Dynamic system '{}' missing executor", m_name);
        finish_dynamic_params(prepared_params);
        return;
    }

    auto status = m_executor->execute(*args);
    finish_dynamic_params(prepared_params);
    if (!status) {
        error("Dynamic system '{}' failed: {}", m_name, status.error().message);
    }
}

DynamicCondition::DynamicCondition(
    std::string name,
    DynamicSystemParams params,
    std::unique_ptr<DynamicConditionExecutor> executor
) :
    m_name(std::move(name)), m_params(std::move(params)),
    m_executor(std::move(executor)),
    m_access(dynamic_system_access_for_params(m_params)) {}

bool DynamicCondition::evaluate(World& world, SystemTicks system_ticks) {
    std::vector<DynamicSystemParam*> prepared_params;
    auto args = prepare_dynamic_params(
        m_name,
        m_params,
        world,
        system_ticks,
        prepared_params
    );
    if (!args) {
        error("{}", args.error().message);
        finish_dynamic_params(prepared_params);
        return false;
    }
    if (!m_executor) {
        error("Dynamic condition '{}' missing executor", m_name);
        finish_dynamic_params(prepared_params);
        return false;
    }

    auto result = m_executor->evaluate(*args);
    finish_dynamic_params(prepared_params);
    if (!result) {
        error(
            "Dynamic condition '{}' failed: {}",
            m_name,
            result.error().message
        );
        return false;
    }
    return *result;
}

} // namespace fei
