#pragma once

#include "base/result.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/system_access.hpp"
#include "refl/ref.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fei {

class World;

struct DynamicSystemError {
    std::string message;
};

class DynamicSystemParam {
  public:
    virtual ~DynamicSystemParam() = default;

    virtual SystemAccess access() const = 0;
    Result<Ref, DynamicSystemError> prepare(World& world);
    virtual Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) = 0;
    virtual void finish() {}
};

using DynamicSystemParamPtr = std::unique_ptr<DynamicSystemParam>;
using DynamicSystemParams = std::vector<DynamicSystemParamPtr>;

SystemAccess
dynamic_system_access_for_params(const DynamicSystemParams& params);

} // namespace fei
