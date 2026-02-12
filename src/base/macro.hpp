#pragma once

// clang-format off
#define FEI_GET_MACRO(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,NAME,...) NAME
#define FEI_FOREACH(action, ...) \
    FEI_GET_MACRO(__VA_ARGS__, \
        FEI_FE_16, FEI_FE_15, FEI_FE_14, FEI_FE_13, FEI_FE_12, FEI_FE_11, FEI_FE_10, FEI_FE_9, \
        FEI_FE_8, FEI_FE_7, FEI_FE_6, FEI_FE_5, FEI_FE_4, FEI_FE_3, FEI_FE_2, FEI_FE_1) \
        (action, __VA_ARGS__)

#define FEI_FE_1(action, x1) action(x1)
#define FEI_FE_2(action, x1, x2) action(x1), action(x2)
#define FEI_FE_3(action, x1, x2, x3) action(x1), action(x2), action(x3)
#define FEI_FE_4(action, x1, x2, x3, x4) action(x1), action(x2), action(x3), action(x4)
#define FEI_FE_5(action, x1, x2, x3, x4, x5) action(x1), action(x2), action(x3), action(x4), action(x5)
#define FEI_FE_6(action, x1, x2, x3, x4, x5, x6) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6)
#define FEI_FE_7(action, x1, x2, x3, x4, x5, x6, x7) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7)
#define FEI_FE_8(action, x1, x2, x3, x4, x5, x6, x7, x8) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8)
#define FEI_FE_9(action, x1, x2, x3, x4, x5, x6, x7, x8, x9) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9)
#define FEI_FE_10(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10)
#define FEI_FE_11(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11)
#define FEI_FE_12(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12)
#define FEI_FE_13(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13)
#define FEI_FE_14(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14)
#define FEI_FE_15(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14), action(x15)
#define FEI_FE_16(action, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16) action(x1), action(x2), action(x3), action(x4), action(x5), action(x6), action(x7), action(x8), action(x9), action(x10), action(x11), action(x12), action(x13), action(x14), action(x15), action(x16)
// clang-format on
