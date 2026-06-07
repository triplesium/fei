#pragma once

#if defined(FEI_REFLGEN_SCRIPT)
#    define FEI_REFLECT [[clang::annotate("reflgen")]]
#else
#    define FEI_REFLECT
#endif
