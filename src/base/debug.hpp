#pragma once
#include "base/log.hpp"

#ifdef NDEBUG
#    define FEI_ASSERT(condition) static_cast<void>(0)
#else
#    define FEI_ASSERT(condition)                                \
        do {                                                     \
            if (!(condition))                                    \
                fei::fatal("Assertion '{}' failed", #condition); \
        } while (0)
#endif
