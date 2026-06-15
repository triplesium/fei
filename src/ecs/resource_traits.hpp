#pragma once

namespace fei {

template<typename T>
struct ResourceTraits {
    static constexpr bool main_thread_only = false;
};

} // namespace fei
