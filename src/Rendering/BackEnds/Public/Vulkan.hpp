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
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats{};
    std::vector<VkPresentModeKHR> presentModes{};
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
        if (auto result = this->create_swap_chain(); !result)
            return result;
        if (auto result = this->create_image_views(); !result)
            return result;
        if (auto result = this->create_render_pass(); !result)
            return result;
        if (auto result = this->create_graphics_pipeline(); !result)
            return result;
        if (auto result = this->create_framebuffers(); !result)
            return result;
        if (auto result = this->create_command_pool(); !result)
            return result;
        if (auto result = this->create_command_buffer(); !result)
            return result;
        if (auto result = this->create_sync_objects(); !result)
            return result;

        return {};
    }

    Result<> draw_frame() noexcept;

    void wait_idle() noexcept;

    void cleanup() noexcept;

  private:
    Result<> setup_debug_messenger();

    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept;

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

  public:
    Result<> create_instance();

    Result<> pick_physical_device();

    bool is_device_suitable(VkPhysicalDevice device) noexcept;

    bool check_device_extension_support(VkPhysicalDevice device) noexcept;

    Result<> create_logical_device();

    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) noexcept;

    Result<> create_surface(GLFWwindow* window) noexcept;

    SwapChainSupportDetails query_swap_chain_support(VkPhysicalDevice device) noexcept;

    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept;

    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept;

    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) noexcept;

    Result<> create_swap_chain();

    Result<> create_image_views();

    Result<> create_graphics_pipeline();

    Result<VkShaderModule> create_shader_module(const std::vector<char>& code) noexcept;

    Result<> create_render_pass();

    Result<> create_framebuffers();

    Result<> create_command_pool();

    Result<> create_command_buffer();

    Result<> record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept;

    Result<> create_sync_objects();

  private:
    static constexpr std::array<const char*, 1> m_validationLayers{
        "VK_LAYER_KHRONOS_validation",
    };
    static constexpr std::array<const char*, 1> m_deviceExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

#ifdef NDEBUG
    static constexpr bool m_enableValidationLayers{false};
#else
    static constexpr bool m_enableValidationLayers{true};
#endif
    VkDebugUtilsMessengerEXT m_debugMessenger{};

    VkInstance m_instance{};

    VkPhysicalDevice m_physicalDevice{};
    VkDevice m_device{};
    VkQueue m_graphicsQueue{};
    VkQueue m_presentQueue{};

    GLFWwindow* m_window{};
    VkSurfaceKHR m_surface{};

    VkSwapchainKHR m_swapChain{};
    std::vector<VkImage> m_swapChainImages{};

    VkFormat m_swapChainImageFormat{};
    VkExtent2D m_swapChainExtent{};

    std::vector<VkImageView> m_swapChainImageViews{};

    VkRenderPass m_renderPass{};
    VkPipelineLayout m_pipelineLayout{};

    VkPipeline m_graphicsPipeline{};

    std::vector<VkFramebuffer> m_swapChainFramebuffers{};

    VkCommandPool m_commandPool{};
    VkCommandBuffer m_commandBuffer{};

    VkSemaphore m_imageAvailableSemaphore{};
    VkSemaphore m_renderFinishedSemaphore{};
    VkFence m_inFlightFence{};
};
