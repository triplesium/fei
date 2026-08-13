#include "app/plugin_id.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace fei {

PluginId::PluginId(std::string qualified_name) :
    m_qualified_name(std::move(qualified_name)) {
    if (m_qualified_name.empty()) {
        throw std::runtime_error("Plugin id cannot be empty");
    }
    if (m_qualified_name.ends_with("::")) {
        throw std::runtime_error(
            "Invalid qualified plugin id '" + m_qualified_name + "'"
        );
    }

    for (std::size_t index = 0; index < m_qualified_name.size(); ++index) {
        if (m_qualified_name[index] != ':') {
            continue;
        }
        if (index == 0 || index + 1 >= m_qualified_name.size() ||
            m_qualified_name[index + 1] != ':' ||
            m_qualified_name[index - 1] == ':') {
            throw std::runtime_error(
                "Invalid qualified plugin id '" + m_qualified_name + "'"
            );
        }
        index += 1;
    }
}

std::string_view PluginId::qualified_name() const {
    return std::string_view(m_qualified_name);
}

std::string_view PluginId::namespace_name() const {
    const auto separator = m_qualified_name.rfind("::");
    if (separator == std::string::npos) {
        return {};
    }
    return std::string_view(m_qualified_name).substr(0, separator);
}

std::string_view PluginId::local_name() const {
    const auto separator = m_qualified_name.rfind("::");
    if (separator == std::string::npos) {
        return std::string_view(m_qualified_name);
    }
    return std::string_view(m_qualified_name).substr(separator + 2);
}

} // namespace fei
