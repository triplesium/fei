#pragma once
#include "base/log.hpp" // IWYU pragma: keep

#ifdef NDEBUG
#    define ETS_ASSERT(condition) static_cast<void>(0)
#else
#    define ETS_ASSERT(condition)                                \
        do {                                                     \
            if (!(condition))                                    \
                ets::fatal("Assertion '{}' failed", #condition); \
        } while (0)
#endif
