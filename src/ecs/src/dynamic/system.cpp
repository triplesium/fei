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

void DynamicSystem::run(World& world) {
    std::vector<Ref> args;
    args.reserve(m_params.size());

    for (auto& param : m_params) {
        if (!param) {
            error("Dynamic system '{}' has null param", m_name);
            return;
        }

        auto arg = param->prepare(world);
        if (!arg) {
            error(
                "Dynamic system '{}' failed to prepare param: {}",
                m_name,
                arg.error().message
            );
            return;
        }
        args.push_back(*arg);
    }

    if (!m_executor) {
        error("Dynamic system '{}' missing executor", m_name);
        return;
    }

    auto status = m_executor->execute(args);
    if (!status) {
        error("Dynamic system '{}' failed: {}", m_name, status.error().message);
    }
}

} // namespace fei
