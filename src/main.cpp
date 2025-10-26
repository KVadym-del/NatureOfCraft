#include "Include/Window.hpp"

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

int main()
{
    Window window{WIDTH, HEIGHT, "NatureOfCraft"};

    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    if (auto code = get_error_code(window.loop()); code != 0)
        return code;

    return 0;
}
