#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "../../Public/IRenderer.hpp"
#include "../../Public/Mesh.hpp"
#include "../../Public/Renderable.hpp"
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanSwapchain.hpp"

#include <functional>
#include <string>
#include <vector>

#include <DirectXMath.h>
#include <vulkan/vulkan.h>

struct MeshData;    // Forward declare — full definition in Assets/Public/MeshData.hpp
struct TextureData; // Forward declare — full definition in Assets/Public/TextureData.hpp

constexpr const int32_t MAX_FRAMES_IN_FLIGHT{2};

enum class KHR_Settings
{
    VSync,
    Triple_Buffering,
    Immediate,
};

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

    Result<uint32_t> upload_texture(const TextureData& textureData) override;

    Result<uint32_t> upload_material(uint32_t albedoTextureIndex, uint32_t normalTextureIndex) override;

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
    /// Returns the UI render pass (used by ImGui).
    inline VkRenderPass get_ui_render_pass() const noexcept
    {
        return m_swapchain.get_ui_render_pass();
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

    // --- Stats getters for overlay ---
    inline std::string get_gpu_name() const noexcept
    {
        return m_vulkanDevice.get_gpu_name();
    }
    inline VkPresentModeKHR get_present_mode() const noexcept
    {
        return m_swapchain.get_present_mode();
    }
    inline VkFormat get_swapchain_format() const noexcept
    {
        return m_swapchain.get_format();
    }
    inline uint32_t get_mesh_count() const noexcept
    {
        return static_cast<uint32_t>(m_meshes.size());
    }
    inline uint32_t get_texture_count() const noexcept
    {
        return static_cast<uint32_t>(m_textures.size());
    }
    inline uint32_t get_material_count() const noexcept
    {
        return static_cast<uint32_t>(m_materials.size());
    }
    inline uint32_t get_renderable_count() const noexcept
    {
        return static_cast<uint32_t>(m_renderables.size());
    }
    uint32_t get_total_triangle_count() const noexcept
    {
        uint32_t total = 0;
        for (const auto& renderable : m_renderables)
        {
            if (renderable.meshIndex < m_meshes.size())
                total += m_meshes[renderable.meshIndex].indexCount / 3;
        }
        return total;
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
        return m_sceneRenderWidth;
    }
    uint32_t get_render_height() const noexcept override
    {
        return m_sceneRenderHeight;
    }

    // --- Runtime settings (IRenderer overrides) ---
    void set_vsync(KHR_Settings mode) noexcept override;
    bool get_vsync() const noexcept override;
    void set_msaa_samples(int samples) noexcept override;
    int get_msaa_samples() const noexcept override;
    void set_render_scale(float scale) noexcept override;
    float get_render_scale() const noexcept override;

  private:
    Result<> create_command_buffers();
    Result<> record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept;
    Result<> create_sync_objects();
    Result<> recreate_swap_chain();
    Result<> create_sampler();
    Result<> create_default_textures();
    Result<> create_descriptor_pool();
    Result<> create_default_material();

    // --- Offscreen scene rendering ---
    Result<> create_scene_render_pass();
    Result<> create_scene_render_target();
    void cleanup_scene_render_target();
    void cleanup_scene_render_pass();
    void compute_scene_render_size();
    VkSampleCountFlagBits get_max_usable_sample_count() const noexcept;

    Result<VkFormat> find_depth_format();
    Result<VkFormat> find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                           VkFormatFeatureFlags features);

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

    std::vector<GpuTexture> m_textures{};
    VkSampler m_sampler{VK_NULL_HANDLE};
    uint32_t m_defaultAlbedoTextureIndex{};
    uint32_t m_defaultNormalTextureIndex{};

    std::vector<GpuMaterial> m_materials{};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    uint32_t m_defaultMaterialIndex{};

    DirectX::XMFLOAT4X4 m_viewMatrix{};
    DirectX::XMFLOAT4X4 m_projMatrix{};

    // --- Offscreen scene render target ---
    VkRenderPass m_sceneRenderPass{VK_NULL_HANDLE};
    VkFramebuffer m_sceneFramebuffer{VK_NULL_HANDLE};

    // Resolve/final color (always 1x sample) — blit source
    VkImage m_sceneColorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_sceneColorMemory{VK_NULL_HANDLE};
    VkImageView m_sceneColorView{VK_NULL_HANDLE};

    // Depth (matches MSAA sample count)
    VkImage m_sceneDepthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_sceneDepthMemory{VK_NULL_HANDLE};
    VkImageView m_sceneDepthView{VK_NULL_HANDLE};

    // MSAA color (only when msaaSamples > 1)
    VkImage m_msaaColorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_msaaColorMemory{VK_NULL_HANDLE};
    VkImageView m_msaaColorView{VK_NULL_HANDLE};

    uint32_t m_sceneRenderWidth{0};
    uint32_t m_sceneRenderHeight{0};

    // --- Settings ---
    float m_renderScale{1.0f};
    VkSampleCountFlagBits m_msaaSamples{VK_SAMPLE_COUNT_1_BIT};
    bool m_vsyncEnabled{false}; // Default: auto (mailbox preferred)
};

NOC_RESTORE_DLL_WARNINGS
