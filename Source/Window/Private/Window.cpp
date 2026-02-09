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

    // Window owns the GLFW user pointer for all callback dispatch
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, glfw_framebuffer_size_callback);
    glfwSetCursorPosCallback(m_window, glfw_cursor_pos_callback);
    glfwSetMouseButtonCallback(m_window, glfw_mouse_button_callback);
    glfwSetScrollCallback(m_window, glfw_scroll_callback);

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

void Window::glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_framebufferSizeCallback)
        self->m_framebufferSizeCallback(width, height);
}

void Window::glfw_cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_cursorPosCallback)
        self->m_cursorPosCallback(xpos, ypos);
}

void Window::glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_mouseButtonCallback)
        self->m_mouseButtonCallback(button, action, mods);
}

void Window::glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_scrollCallback)
        self->m_scrollCallback(xoffset, yoffset);
}
