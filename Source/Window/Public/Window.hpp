#pragma once
#include "../../Core/Public/Expected.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <functional>
#include <string_view>

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT Window
{
  public:
    using FramebufferSizeCallback = std::function<void(int width, int height)>;
    using CursorPosCallback = std::function<void(double xpos, double ypos)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;

    inline Window(std::uint32_t width, std::uint32_t height, std::string_view windowTitle)
        : m_width(width), m_height(height), m_windowTitle(windowTitle)
    {}

    inline ~Window() noexcept
    {
        this->cleanup();
    };

    Result<> init();

    Result<> loop(std::function<Result<>()> draw_frame);

    void cleanup() noexcept;

  public:
    inline GLFWwindow* get_glfw_window() const noexcept
    {
        return m_window;
    }

    void set_framebuffer_size_callback(FramebufferSizeCallback cb) noexcept
    {
        m_framebufferSizeCallback = std::move(cb);
    }
    void set_cursor_pos_callback(CursorPosCallback cb) noexcept
    {
        m_cursorPosCallback = std::move(cb);
    }
    void set_mouse_button_callback(MouseButtonCallback cb) noexcept
    {
        m_mouseButtonCallback = std::move(cb);
    }
    void set_scroll_callback(ScrollCallback cb) noexcept
    {
        m_scrollCallback = std::move(cb);
    }

  private:
    static void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void glfw_cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
    static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

  private:
    std::uint32_t m_width{};
    std::uint32_t m_height{};
    std::string_view m_windowTitle{};

    GLFWwindow* m_window{};

    FramebufferSizeCallback m_framebufferSizeCallback{};
    CursorPosCallback m_cursorPosCallback{};
    MouseButtonCallback m_mouseButtonCallback{};
    ScrollCallback m_scrollCallback{};
};

NOC_RESTORE_DLL_WARNINGS
