#pragma once
#include "../../../Core/Public/Expected.hpp"

#include <array>
#include <functional>
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

constexpr const int32_t MAX_FRAMES_IN_FLIGHT{2};

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

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT Vulkan
{
  public:
    inline Vulkan(GLFWwindow* window) : m_window(window)
    {}
    inline ~Vulkan()
    {
        this->cleanup();
    };

  public:
    Result<> initialize() noexcept
    {
        if (m_window == nullptr)
        {
            return make_error("GLFW window is null", ErrorCode::VulkanGLFWWindowIsNull);
        }
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebuffer_resize_callback);

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
        if (auto result = this->create_command_buffers(); !result)
            return result;
        if (auto result = this->create_sync_objects(); !result)
            return result;

        return {};
    }

    Result<> draw_frame() noexcept;

    void wait_idle() noexcept;

    void cleanup() noexcept;

  private:
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

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

    void cleanup_swap_chain();

    Result<> recreate_swap_chain();

    Result<> create_image_views();

    Result<> create_graphics_pipeline();

    Result<VkShaderModule> create_shader_module(const std::vector<char>& code) noexcept;

    Result<> create_render_pass();

    Result<> create_framebuffers();

    Result<> create_command_pool();

    Result<> create_command_buffers();

    Result<> record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept;

    Result<> create_sync_objects();

    // Media
    Result<> init_video_texture(uint32_t width, uint32_t height);
    void update_video_texture(const uint8_t* pixels);

    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                       VkDeviceMemory& bufferMemory);


  public:
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
    inline VkRenderPass get_render_pass() const noexcept
    {
        return m_renderPass;
    }
    inline VkExtent2D get_swapchain_extent() const noexcept
    {
        return m_swapChainExtent;
    }
    inline uint32_t get_swapchain_image_count() const noexcept
    {
        return static_cast<uint32_t>(m_swapChainImages.size());
    }
    inline VkCommandPool get_command_pool() const noexcept
    {
        return m_commandPool;
    }
    inline uint32_t get_current_frame_index() const noexcept
    {
        return m_currentFrame;
    }
    inline uint32_t get_graphics_queue_family_index() noexcept
    {
        return find_queue_families(m_physicalDevice).graphicsFamily.value();
    }

    using UIRenderCallback = std::function<void(VkCommandBuffer)>;
    inline void set_ui_render_callback(const UIRenderCallback& cb) noexcept
    {
        m_uiRenderCallback = cb;
    }

    inline const UIRenderCallback& get_ui_render_callback() const noexcept
    {
        return m_uiRenderCallback;
    }

    VkDescriptorSet get_video_descriptor_set() const
    {
        return m_videoDescriptorSet;
    }

    VkSampler get_video_sampler() const
    {
        return m_videoSampler;
    }

    VkImageView get_video_image_view() const
    {
        return m_videoImageView;
    }

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
    std::vector<VkCommandBuffer> m_commandBuffers{};

    std::vector<VkSemaphore> m_imageAvailableSemaphores{};
    std::vector<VkFence> m_inFlightFences{};
    
    std::vector<VkSemaphore> m_renderFinishedSemaphores{};
    std::vector<VkFence> m_imagesInFlight{};

    bool m_framebufferResized{false};

    uint32_t m_currentFrame{};

    UIRenderCallback m_uiRenderCallback{};

    VkImage m_videoImage{};
    VkDeviceMemory m_videoImageMemory{};
    VkImageView m_videoImageView{};
    VkSampler m_videoSampler{};
    VkDescriptorSet m_videoDescriptorSet{};

    VkBuffer m_stagingBuffer{};
    VkDeviceMemory m_stagingBufferMemory{};
    uint32_t m_videoWidth{};
    uint32_t m_videoHeight{};
};

NOC_RESTORE_DLL_WARNINGS
