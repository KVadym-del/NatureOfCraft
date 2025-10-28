#pragma once
#include "../../../Core/Public/Expected.hpp"

#include <array>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif // _WIN32
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif // _WIN32
#include <GLFW/glfw3native.h>

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    inline bool is_complete() const noexcept
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
        ;
    }
};

class Vulkan
{
  public:
    inline Vulkan(GLFWwindow* window) : m_window(window)
    {}
    ~Vulkan() = default;

  public:
    Result<> initialize() noexcept
    {
        if (auto result = this->create_instance(); !result)
            return result;
        if (auto result = this->setup_debug_messenger(); !result)
            return result;
        if (auto result = this->create_surface(this->m_window); !result)
            return result;
        if (auto result = this->pick_physical_device(); !result)
            return result;
        if (auto result = this->create_logical_device(); !result)
            return result;

        return {};
    }

    void cleanup() noexcept;

  private:
    Result<> create_instance();

    Result<> setup_debug_messenger();

    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept;

    Result<> pick_physical_device();

    bool is_device_suitable(VkPhysicalDevice device) noexcept;

    Result<> create_logical_device();

    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) noexcept;

    Result<> create_surface(GLFWwindow* window) noexcept;

    bool check_validation_layer_support() const noexcept;

    std::vector<const char*> get_required_extensions();

    static VKAPI_ATTR uint32_t VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                         VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                         void* pUserData);

    VkResult ceate_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDebugUtilsMessengerEXT* pDebugMessenger) noexcept;

    void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                           const VkAllocationCallbacks* pAllocator) noexcept;

  private:
    VkInstance m_instance{};

    static constexpr std::array<const char*, 1> m_validationLayers{"VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
    static constexpr bool m_enableValidationLayers{false};
#else
    static constexpr bool m_enableValidationLayers{true};
#endif
    VkDebugUtilsMessengerEXT m_debugMessenger{};

    VkPhysicalDevice m_physicalDevice{};
    VkDevice m_device{};
    VkQueue m_graphicsQueue{};
    VkQueue m_presentQueue{};

    GLFWwindow* m_window{};
    VkSurfaceKHR m_surface{};
};
