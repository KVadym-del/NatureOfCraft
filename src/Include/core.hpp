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
    {                                                                                                                  \
    }
DEFINE_VERSION(NATURE_OF_CRAFT, 0, 0, 1);
