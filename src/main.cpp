#include "Include/expected.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <fmt/base.h>

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

int main()
{
    if (!glfwInit())
    {
        fmt::println("ERROR: Failed to initialize GLFW.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan window", nullptr, nullptr);
    if (!window)
    {
        fmt::println("ERROR: Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
