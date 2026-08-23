#pragma once

// clang-format off
#define ETS_EXPAND(x) x
#define ETS_GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,NAME,...) NAME
#define ETS_FOREACH(action, ...) \
    ETS_EXPAND(ETS_GET_MACRO(__VA_ARGS__, \
        ETS_FE_16, ETS_FE_15, ETS_FE_14, ETS_FE_13, ETS_FE_12, ETS_FE_11, ETS_FE_10, ETS_FE_9, \
        ETS_FE_8, ETS_FE_7, ETS_FE_6, ETS_FE_5, ETS_FE_4, ETS_FE_3, ETS_FE_2, ETS_FE_1) \
        (action, __VA_ARGS__))

#define ETS_FE_1(action, x1) action(x1)
#define ETS_FE_2(action, x1, x2) action(x1), action(x2)
#define ETS_FE_3(action, x1, x2, x3) action(x1), action(x2), action(x3)
#define ETS_FE_4(action, x1, x2, x3, x4) action(x1), action(x2), action(x3), action(x4)
#define ETS_FE_5(action, x1, x2, x3, x4, x5) action(x1), action(x2), action(x3), action(x4), action(x5)
#define ETS_FE_6(action, x1, x2, x3, x4, x5, x6) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6)
#define ETS_FE_7(action, x1, x2, x3, x4, x5, x6, x7) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7)
#define ETS_FE_8(action, x1, x2, x3, x4, x5, x6, x7, x8) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8)
#define ETS_FE_9(action, x1, x2, x3, x4, x5, x6, x7, x8, x9) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9)
#define ETS_FE_10(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10)
#define ETS_FE_11(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11)
#define ETS_FE_12(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12)
#define ETS_FE_13(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13)
#define ETS_FE_14(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14)
#define ETS_FE_15(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14), action(x15)
#define ETS_FE_16(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14), action(x15), action(x16)
// clang-format on
