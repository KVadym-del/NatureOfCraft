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

  private:
    std::uint32_t m_width{};
    std::uint32_t m_height{};
    std::string_view m_windowTitle{};

    GLFWwindow* m_window{};
};

NOC_RESTORE_DLL_WARNINGS
