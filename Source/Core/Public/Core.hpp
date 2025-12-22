#pragma once
#include <cstdint>

#define STRINGIFY_DETAIL(x) #x
#define STRINGIFY(x) STRINGIFY_DETAIL(x)

#define DEFINE_VERSION(VarName, Major, Minor, Patch)                                                                   \
    constexpr const struct                                                                                             \
    {                                                                                                                  \
        const uint32_t major{Major};                                                                                   \
        const uint32_t minor{Minor};                                                                                   \
        const uint32_t patch{Patch};                                                                                   \
                                                                                                                       \
        constexpr operator const char*() const noexcept                                                                \
        {                                                                                                              \
            return STRINGIFY(Major) "." STRINGIFY(Minor) "." STRINGIFY(Patch);                                         \
        }                                                                                                              \
    } VarName                                                                                                          \
    {}
DEFINE_VERSION(NATURE_OF_CRAFT, 0, 0, 1);

#ifdef Source_EXPORTS
#define NOC_EXPORT __declspec(dllexport)
#else
#define NOC_EXPORT __declspec(dllimport)
#endif

// Disable C4251 warning for STL types in exported classes
#ifdef _MSC_VER
#define NOC_SUPPRESS_DLL_WARNINGS \
    __pragma(warning(push)) \
    __pragma(warning(disable: 4251))
#define NOC_RESTORE_DLL_WARNINGS \
    __pragma(warning(pop))
#else
#define NOC_SUPPRESS_DLL_WARNINGS
#define NOC_RESTORE_DLL_WARNINGS
#endif