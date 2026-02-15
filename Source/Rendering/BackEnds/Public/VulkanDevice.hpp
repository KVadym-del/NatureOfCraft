#pragma once
#include "../../../Core/Public/Expected.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
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

struct DeviceLocalMemoryBudget
{
    bool supported{false};
    uint64_t usageBytes{0};
    uint64_t budgetBytes{0};
};

NOC_SUPPRESS_DLL_WARNINGS

/// Owns the Vulkan instance, physical/logical device, surface, queues, command pool,
/// and provides utility methods for buffer/image creation.
class NOC_EXPORT VulkanDevice
{
  public:
    explicit VulkanDevice(GLFWwindow* window) : m_window(window)
    {}
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    /// Initializes the full device stack: instance, debug messenger, surface,
    /// physical device, logical device, command pool.
    Result<> initialize() noexcept;

    /// Destroys all owned Vulkan objects in reverse order.
    void cleanup() noexcept;

    // --- Query helpers ---
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device) noexcept;
    SwapChainSupportDetails query_swap_chain_support(VkPhysicalDevice device) noexcept;

    // --- Utility methods used by other components ---
    Result<uint32_t> find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    Result<> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                           VkBuffer& buffer, VkDeviceMemory& bufferMemory, VkDeviceSize* outAllocationBytes = nullptr);

    /// Allocates and begins a one-time command buffer from the main command pool.
    Result<VkCommandBuffer> begin_single_time_commands();

    /// Ends, submits, waits, and frees a one-time command buffer.
    Result<> end_single_time_commands(VkCommandBuffer commandBuffer);

    Result<> copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    Result<> create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                          VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                          VkDeviceMemory& imageMemory, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                          VkDeviceSize* outAllocationBytes = nullptr);

    Result<VkImageView> create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    Result<> transition_image_layout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

    Result<> copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    // --- Getters ---
    inline VkInstance get_instance() const noexcept
    {
        return m_instance;
    }
    inline VkPhysicalDevice get_physical_device() const noexcept
    {
        return m_physicalDevice;
    }
    inline VkDevice get_device() const noexcept
    {
        return m_device;
    }
    inline VkQueue get_graphics_queue() const noexcept
    {
        return m_graphicsQueue;
    }
    inline VkQueue get_present_queue() const noexcept
    {
        return m_presentQueue;
    }
    inline VkSurfaceKHR get_surface() const noexcept
    {
        return m_surface;
    }
    inline VkCommandPool get_command_pool() const noexcept
    {
        return m_commandPool;
    }
    inline GLFWwindow* get_window() const noexcept
    {
        return m_window;
    }
    inline uint32_t get_graphics_queue_family_index() noexcept
    {
        auto indices = find_queue_families(m_physicalDevice);
        return indices.graphicsFamily.value_or(0);
    }
    inline std::string get_gpu_name() const noexcept
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        return properties.deviceName;
    }
    DeviceLocalMemoryBudget get_device_local_memory_budget() const noexcept;

  private:
    Result<> create_instance();
    Result<> setup_debug_messenger();
    Result<> create_surface(GLFWwindow* window) noexcept;
    Result<> pick_physical_device();
    Result<> create_logical_device();
    Result<> create_command_pool();

    bool is_device_suitable(VkPhysicalDevice device) noexcept;
    bool check_device_extension_support(VkPhysicalDevice device) noexcept;
    bool check_validation_layer_support() const noexcept;
    std::vector<const char*> get_required_extensions();

    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept;

    static VKAPI_ATTR uint32_t VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                         VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                         void* pUserData);

    VkResult create_debug_utils_messenger_ext(VkInstance instance,
                                              const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDebugUtilsMessengerEXT* pDebugMessenger) noexcept;

    void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                           const VkAllocationCallbacks* pAllocator) noexcept;

    // --- Members ---
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

    GLFWwindow* m_window{};

    VkInstance m_instance{};
    VkDebugUtilsMessengerEXT m_debugMessenger{};

    VkSurfaceKHR m_surface{};

    VkPhysicalDevice m_physicalDevice{};
    VkDevice m_device{};

    VkQueue m_graphicsQueue{};
    VkQueue m_presentQueue{};

    VkCommandPool m_commandPool{};
    bool m_hasMemoryBudgetExtension{false};
    PFN_vkGetPhysicalDeviceMemoryProperties2 m_getPhysicalDeviceMemoryProperties2{nullptr};
    PFN_vkGetPhysicalDeviceMemoryProperties2KHR m_getPhysicalDeviceMemoryProperties2KHR{nullptr};
    mutable DeviceLocalMemoryBudget m_cachedMemoryBudget{};
    mutable std::chrono::steady_clock::time_point m_lastMemoryBudgetQuery{};
    mutable bool m_hasCachedMemoryBudget{false};
    static constexpr std::chrono::milliseconds MemoryBudgetCacheInterval{250};
};

NOC_RESTORE_DLL_WARNINGS
