#include "Include/Window.hpp"

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
    while (!glfwWindowShouldClose(this->m_window))
    {
        glfwPollEvents();
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
