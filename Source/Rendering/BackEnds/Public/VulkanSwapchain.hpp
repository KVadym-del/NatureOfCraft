#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "VulkanDevice.hpp"

#include <vector>

#include <vulkan/vulkan.h>

NOC_SUPPRESS_DLL_WARNINGS

/// Owns the Vulkan swapchain, image views, UI render pass, and UI framebuffers.
/// The scene render pass and depth resources are now managed by the Vulkan orchestrator.
class NOC_EXPORT VulkanSwapchain
{
  public:
    explicit VulkanSwapchain(VulkanDevice& device) : m_device(device)
    {}
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    /// Creates the swapchain, image views, UI render pass, and UI framebuffers.
    Result<> initialize();

    /// Destroys all swapchain-related resources.
    void cleanup();

    /// Full cleanup + recreate cycle. Does NOT manage sync objects (caller handles those).
    Result<> recreate();

    /// Set desired present mode for next swapchain recreation.
    void set_desired_present_mode(VkPresentModeKHR mode) noexcept
    {
        m_desiredPresentMode = mode;
    }

    // --- Getters ---
    inline VkSwapchainKHR get_swapchain() const noexcept
    {
        return m_swapChain;
    }
    inline VkRenderPass get_ui_render_pass() const noexcept
    {
        return m_uiRenderPass;
    }
    inline VkExtent2D get_extent() const noexcept
    {
        return m_swapChainExtent;
    }
    inline VkFormat get_format() const noexcept
    {
        return m_swapChainImageFormat;
    }
    inline VkFramebuffer get_ui_framebuffer(uint32_t index) const noexcept
    {
        return m_uiFramebuffers[index];
    }
    inline uint32_t get_image_count() const noexcept
    {
        return static_cast<uint32_t>(m_swapChainImages.size());
    }
    inline VkPresentModeKHR get_present_mode() const noexcept
    {
        return m_presentMode;
    }
    inline const std::vector<VkImage>& get_images() const noexcept
    {
        return m_swapChainImages;
    }
    inline const std::vector<VkImageView>& get_image_views() const noexcept
    {
        return m_swapChainImageViews;
    }

  private:
    Result<> create_swap_chain();
    void cleanup_swap_chain();
    Result<> create_image_views();
    Result<> create_ui_render_pass();
    Result<> create_ui_framebuffers();

    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept;
    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept;
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) noexcept;

    // --- Members ---
    VulkanDevice& m_device;

    VkSwapchainKHR m_swapChain{};
    std::vector<VkImage> m_swapChainImages{};
    VkFormat m_swapChainImageFormat{};
    VkExtent2D m_swapChainExtent{};
    VkPresentModeKHR m_presentMode{};
    VkPresentModeKHR m_desiredPresentMode{VK_PRESENT_MODE_MAX_ENUM_KHR}; // MAX_ENUM = auto-select
    std::vector<VkImageView> m_swapChainImageViews{};

    VkRenderPass m_uiRenderPass{};
    std::vector<VkFramebuffer> m_uiFramebuffers{};
};

NOC_RESTORE_DLL_WARNINGS
