#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fei {

struct SystemProfileInfo {
    std::string name;
    std::string file;
    std::string function;
    std::uint32_t line = 0;

    bool named() const { return !name.empty(); }

    static SystemProfileInfo from_source_location(
        std::string_view name,
        const std::source_location& location
    ) {
        return SystemProfileInfo {
            .name = std::string(name),
            .file = location.file_name(),
            .function = location.function_name(),
            .line = location.line(),
        };
    }
};

template<typename Func>
concept ProfileHashableSystem =
    std::is_pointer_v<std::remove_cvref_t<Func>> &&
    std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<Func>>>;

template<ProfileHashableSystem Func>
std::size_t system_profile_hash(Func func) {
    return reinterpret_cast<std::size_t>(func);
}

class SystemProfileRegistry {
  private:
    std::unordered_map<std::size_t, SystemProfileInfo> m_profiles;

  public:
    static SystemProfileRegistry& instance();

    void register_system(std::size_t hash, SystemProfileInfo info);

    template<ProfileHashableSystem Func>
    void register_system(Func func, SystemProfileInfo info) {
        register_system(system_profile_hash(func), std::move(info));
    }

    std::optional<SystemProfileInfo> find(std::size_t hash) const;
    std::optional<SystemProfileInfo> symbolize(std::size_t address) const;
    void clear();
};

template<typename Func>
struct NamedSystem {
    using FuncType = Func;

    std::string name;
    Func func;
    std::source_location location;
};

template<typename T>
struct IsNamedSystem : std::false_type {};

template<typename Func>
struct IsNamedSystem<NamedSystem<Func>> : std::true_type {};

template<typename T>
concept NamedSystemWrapper = IsNamedSystem<std::remove_cvref_t<T>>::value;

template<typename Func>
NamedSystem<std::decay_t<Func>> named_system(
    std::string_view name,
    Func&& func,
    std::source_location location = std::source_location::current()
) {
    return NamedSystem<std::decay_t<Func>> {
        .name = std::string(name),
        .func = std::forward<Func>(func),
        .location = location,
    };
}

} // namespace fei

#define FEI_NAMED_SYSTEM(fn) \
    ::fei::named_system(#fn, fn, std::source_location::current())

#define FEI_SYSTEM_NAME(name, func) \
    ::fei::named_system(name, func, std::source_location::current())
