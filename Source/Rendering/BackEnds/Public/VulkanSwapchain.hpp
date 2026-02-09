#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "VulkanDevice.hpp"

#include <vector>

#include <vulkan/vulkan.h>

NOC_SUPPRESS_DLL_WARNINGS

/// Owns the Vulkan swapchain, image views, render pass, depth resources, and framebuffers.
class NOC_EXPORT VulkanSwapchain
{
  public:
    explicit VulkanSwapchain(VulkanDevice& device) : m_device(device)
    {}
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    /// Creates the swapchain, image views, render pass, depth resources, and framebuffers.
    Result<> initialize();

    /// Destroys all swapchain-related resources (image views, depth, framebuffers, swapchain).
    void cleanup();

    /// Full cleanup + recreate cycle. Does NOT manage sync objects (caller handles those).
    Result<> recreate();

    // --- Getters ---
    inline VkSwapchainKHR get_swapchain() const noexcept
    {
        return m_swapChain;
    }
    inline VkRenderPass get_render_pass() const noexcept
    {
        return m_renderPass;
    }
    inline VkExtent2D get_extent() const noexcept
    {
        return m_swapChainExtent;
    }
    inline VkFormat get_format() const noexcept
    {
        return m_swapChainImageFormat;
    }
    inline VkFramebuffer get_framebuffer(uint32_t index) const noexcept
    {
        return m_swapChainFramebuffers[index];
    }
    inline uint32_t get_image_count() const noexcept
    {
        return static_cast<uint32_t>(m_swapChainImages.size());
    }
    inline const std::vector<VkImage>& get_images() const noexcept
    {
        return m_swapChainImages;
    }

  private:
    Result<> create_swap_chain();
    void cleanup_swap_chain();
    Result<> create_image_views();
    Result<> create_render_pass();
    Result<> create_framebuffers();
    Result<> create_depth_resources();

    Result<VkFormat> find_depth_format();
    Result<VkFormat> find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                           VkFormatFeatureFlags features);
    bool has_stencil_component(VkFormat format);

    VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept;
    VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept;
    VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) noexcept;

    // --- Members ---
    VulkanDevice& m_device;

    VkSwapchainKHR m_swapChain{};
    std::vector<VkImage> m_swapChainImages{};
    VkFormat m_swapChainImageFormat{};
    VkExtent2D m_swapChainExtent{};
    std::vector<VkImageView> m_swapChainImageViews{};

    VkRenderPass m_renderPass{};

    std::vector<VkFramebuffer> m_swapChainFramebuffers{};

    VkImage m_depthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_depthImageMemory{VK_NULL_HANDLE};
    VkImageView m_depthImageView{VK_NULL_HANDLE};
};

NOC_RESTORE_DLL_WARNINGS
