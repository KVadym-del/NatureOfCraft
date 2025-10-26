#include "Expected.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <fmt/base.h>
#include <string_view>

class Window
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

    Result<> loop();

    void cleanup() noexcept;

  public:
    inline GLFWwindow* getGLFWwindow() const noexcept
    {
        return m_window;
    }

  private:
    std::uint32_t m_width{};
    std::uint32_t m_height{};
    std::string_view m_windowTitle{};

    GLFWwindow* m_window{};
};
