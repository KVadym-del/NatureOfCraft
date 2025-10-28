#include "../Public/Window.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

Result<> Window::init()
{
    if (!glfwInit())
    {
        return make_error("Failed to initialize GLFW", ErrorCode::GLFWInitializationFailed);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    this->m_window = glfwCreateWindow(this->m_width, this->m_height, this->m_windowTitle.data(), nullptr, nullptr);
    if (!this->m_window)
    {
        glfwTerminate();
        return make_error("Failed to create GLFW window", ErrorCode::GLFWFailedToCreateWindow);
    }

    return {};
}

Result<> Window::loop()
{
    std::atomic<bool> shouldExit{false};

    std::thread inputThread([&shouldExit]() {
        std::string input{};
        while (!shouldExit)
        {
            if (std::getline(std::cin, input))
            {
                if (input == "exit")
                {
                    shouldExit = true;
                    break;
                }
            }
        }
    });

    while (!glfwWindowShouldClose(this->m_window) && !shouldExit)
    {
        glfwPollEvents();

        if (glfwGetKey(this->m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(this->m_window, GLFW_TRUE);
        }
    }

    shouldExit = true;
    if (inputThread.joinable())
    {
        inputThread.detach();
    }

    return {};
}

void Window::cleanup() noexcept
{
    if (!this->m_window)
    {
        glfwDestroyWindow(this->m_window);
    }
    glfwTerminate();
}
