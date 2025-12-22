#include "../Public/Window.hpp"

Result<> Window::init()
{
    if (!glfwInit())
    {
        return make_error("Failed to initialize GLFW", ErrorCode::GLFWInitializationFailed);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    this->m_window = glfwCreateWindow(this->m_width, this->m_height, this->m_windowTitle.data(), nullptr, nullptr);
    if (!this->m_window)
    {
        glfwTerminate();
        return make_error("Failed to create GLFW window", ErrorCode::GLFWFailedToCreateWindow);
    }

    return {};
}

Result<> Window::loop(std::function<Result<>()> draw_frame)
{
    while (!glfwWindowShouldClose(this->m_window))
    {
        glfwPollEvents();

        if (glfwGetKey(this->m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(this->m_window, GLFW_TRUE);
        }

        auto drawResult = draw_frame();
        if (!drawResult)
            return drawResult;
    }

    return {};
}

void Window::cleanup() noexcept
{
    if (this->m_window)
    {
        glfwDestroyWindow(this->m_window);
        this->m_window = nullptr;
    }
    glfwTerminate();
}
