#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "../../Public/IRenderer.hpp"
#include "../../Public/Mesh.hpp"
#include "../../Public/Renderable.hpp"
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanSwapchain.hpp"

#include <functional>
#include <vector>

#include <DirectXMath.h>
#include <vulkan/vulkan.h>

struct MeshData; // Forward declare — full definition in Assets/Public/MeshData.hpp

constexpr const int32_t MAX_FRAMES_IN_FLIGHT{2};

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT Vulkan : public IRenderer
{
  public:
    inline Vulkan(GLFWwindow* window) : m_vulkanDevice(window), m_swapchain(m_vulkanDevice), m_pipeline(m_vulkanDevice)
    {}
    inline ~Vulkan()
    {
        this->cleanup();
    };

  public:
    Result<> initialize() noexcept override;

    Result<> draw_frame() noexcept override;

    void wait_idle() noexcept override;

    void cleanup() noexcept;

    Result<uint32_t> upload_mesh(const MeshData& meshData) override;

    // --- Delegating getters for ImGuiLayer and other consumers ---
    inline VkInstance get_instance() const noexcept
    {
        return m_vulkanDevice.get_instance();
    }
    inline VkPhysicalDevice get_physical_device() const noexcept
    {
        return m_vulkanDevice.get_physical_device();
    }
    inline VkDevice get_device() const noexcept
    {
        return m_vulkanDevice.get_device();
    }
    inline VkQueue get_graphics_queue() const noexcept
    {
        return m_vulkanDevice.get_graphics_queue();
    }
    inline VkQueue get_present_queue() const noexcept
    {
        return m_vulkanDevice.get_present_queue();
    }
    inline VkRenderPass get_render_pass() const noexcept
    {
        return m_swapchain.get_render_pass();
    }
    inline VkExtent2D get_swapchain_extent() const noexcept
    {
        return m_swapchain.get_extent();
    }
    inline uint32_t get_swapchain_image_count() const noexcept
    {
        return m_swapchain.get_image_count();
    }
    inline VkCommandPool get_command_pool() const noexcept
    {
        return m_vulkanDevice.get_command_pool();
    }
    inline uint32_t get_current_frame_index() const noexcept
    {
        return m_currentFrame;
    }
    inline uint32_t get_graphics_queue_family_index() noexcept
    {
        return m_vulkanDevice.get_graphics_queue_family_index();
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

    using SwapchainRecreatedCallback = std::function<void()>;
    inline void set_swapchain_recreated_callback(const SwapchainRecreatedCallback& cb) noexcept
    {
        m_swapchainRecreatedCallback = cb;
    }

    /// Sets the view and projection matrices used for rendering.
    /// Call this each frame before draw_frame().
    void set_view_projection(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj) noexcept override
    {
        DirectX::XMStoreFloat4x4(&m_viewMatrix, view);
        DirectX::XMStoreFloat4x4(&m_projMatrix, proj);
    }

    /// Sets the list of renderables to draw this frame.
    /// Each Renderable contains a world matrix and a mesh index.
    void set_renderables(std::vector<Renderable> renderables) noexcept override
    {
        m_renderables = std::move(renderables);
    }

    /// Notifies the renderer that the framebuffer was resized.
    /// Called by the Window's framebuffer size callback.
    void on_framebuffer_resized() noexcept override
    {
        m_framebufferResized = true;
    }

    uint32_t get_render_width() const noexcept override
    {
        return m_swapchain.get_extent().width;
    }
    uint32_t get_render_height() const noexcept override
    {
        return m_swapchain.get_extent().height;
    }

  private:
    Result<> create_command_buffers();
    Result<> record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept;
    Result<> create_sync_objects();
    Result<> recreate_swap_chain();

    // --- Sub-components ---
    VulkanDevice m_vulkanDevice;
    VulkanSwapchain m_swapchain;
    VulkanPipeline m_pipeline;

    // --- Owned by Vulkan (orchestration) ---
    std::vector<VkCommandBuffer> m_commandBuffers{};

    std::vector<VkSemaphore> m_imageAvailableSemaphores{};
    std::vector<VkFence> m_inFlightFences{};

    std::vector<VkSemaphore> m_renderFinishedSemaphores{};
    std::vector<VkFence> m_imagesInFlight{};

    bool m_framebufferResized{false};

    uint32_t m_currentFrame{};

    UIRenderCallback m_uiRenderCallback{};
    SwapchainRecreatedCallback m_swapchainRecreatedCallback{};

    std::vector<Mesh> m_meshes{};
    std::vector<Renderable> m_renderables{};

    DirectX::XMFLOAT4X4 m_viewMatrix{};
    DirectX::XMFLOAT4X4 m_projMatrix{};
};

NOC_RESTORE_DLL_WARNINGS
