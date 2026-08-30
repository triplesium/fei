#pragma once

#include "base/result.hpp"
#include "refl/callable.hpp"
#include "refl/conversion.hpp"

namespace ets {

[[nodiscard]] ConversionRank
reflected_conversion_rank(TypeId target_type, const Ref& source);

[[nodiscard]] Result<Val, InvokeFailure>
reflected_convert(TypeId target_type, const Ref& source);

} // namespace ets
