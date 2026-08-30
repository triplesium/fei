#pragma once

#include "model.hpp"

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ets::luau_defgen {

class TypeMapper {
  public:
    explicit TypeMapper(const Database& database);

    [[nodiscard]] std::string map(std::string_view cpp_type);
    [[nodiscard]] const std::set<std::string>& unsupported_types() const;

  private:
    std::unordered_map<std::string, std::string> m_reflected_types;
    std::set<std::string> m_unsupported_types;
};

[[nodiscard]] std::string internal_type_name(std::string_view cpp_name);
[[nodiscard]] std::string internal_value_name(std::string_view cpp_name);

} // namespace ets::luau_defgen
