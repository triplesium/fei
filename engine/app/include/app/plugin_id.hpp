#pragma once

#include <string>
#include <string_view>

namespace ets {

class PluginId {
  private:
    std::string m_qualified_name;

  public:
    explicit PluginId(std::string qualified_name);

    [[nodiscard]] std::string_view qualified_name() const;
    [[nodiscard]] std::string_view namespace_name() const;
    [[nodiscard]] std::string_view local_name() const;

    bool operator==(const PluginId& other) const = default;
};

} // namespace ets
