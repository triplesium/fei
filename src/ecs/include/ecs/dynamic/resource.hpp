#pragma once

#include "ecs/dynamic/access.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "refl/type.hpp"

#include <string>

namespace fei {

class DynamicResourceParam final : public DynamicSystemParam {
  public:
    std::string name;
    TypeId type;
    DynamicParamAccess param_access {DynamicParamAccess::Read};
    bool optional {false};

    DynamicResourceParam(
        std::string name,
        TypeId type,
        DynamicParamAccess access = DynamicParamAccess::Read,
        bool optional = false
    );

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError> prepare(World& world) override;
};

} // namespace fei
