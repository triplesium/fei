#include "ecs/dynamic/system.hpp"

#include "base/log.hpp"
#include "ecs/world.hpp"

#include <utility>
#include <vector>

namespace fei {

DynamicSystem::DynamicSystem(
    std::string name,
    DynamicSystemParams params,
    std::unique_ptr<DynamicSystemExecutor> executor
) :
    m_name(std::move(name)), m_params(std::move(params)),
    m_executor(std::move(executor)),
    m_access(dynamic_system_access_for_params(m_params)) {}

void DynamicSystem::execute(World& world, SystemTicks system_ticks) {
    std::vector<Ref> args;
    args.reserve(m_params.size());
    std::vector<DynamicSystemParam*> prepared_params;
    prepared_params.reserve(m_params.size());

    auto finish_params = [&prepared_params]() {
        for (auto* param : prepared_params) {
            param->finish();
        }
        prepared_params.clear();
    };

    for (auto& param : m_params) {
        if (!param) {
            error("Dynamic system '{}' has null param", m_name);
            finish_params();
            return;
        }

        auto arg = param->prepare(world, system_ticks);
        if (!arg) {
            error(
                "Dynamic system '{}' failed to prepare param: {}",
                m_name,
                arg.error().message
            );
            finish_params();
            return;
        }
        args.push_back(*arg);
        prepared_params.push_back(param.get());
    }

    if (!m_executor) {
        error("Dynamic system '{}' missing executor", m_name);
        finish_params();
        return;
    }

    auto status = m_executor->execute(args);
    finish_params();
    if (!status) {
        error("Dynamic system '{}' failed: {}", m_name, status.error().message);
    }
}

} // namespace fei
