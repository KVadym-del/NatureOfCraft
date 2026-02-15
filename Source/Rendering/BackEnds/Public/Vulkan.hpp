#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "../../Public/IRenderer.hpp"
#include "../../Public/Mesh.hpp"
#include "../../Public/Renderable.hpp"
#include "VulkanDevice.hpp"
#include "VulkanPipeline.hpp"
#include "VulkanSwapchain.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
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

    Result<> clear_scene_content() noexcept override;

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
    inline VkSampler get_sampler() const noexcept
    {
        return m_sampler;
    }
    /// Returns the UI render pass (used by ImGui).
    inline VkRenderPass get_ui_render_pass() const noexcept
    {
        return m_swapchain.get_ui_render_pass();
    }
    inline VkImageView get_scene_color_image_view() const noexcept
    {
        return m_sceneColorView;
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
        return m_totalTriangleCountCached;
    }
    inline uint32_t get_last_visible_renderable_count() const noexcept
    {
        return m_lastVisibleRenderableCount;
    }
    inline uint32_t get_last_culled_renderable_count() const noexcept
    {
        return m_lastCulledRenderableCount;
    }
    inline uint32_t get_last_draw_call_count() const noexcept
    {
        return m_lastDrawCallCount;
    }
    inline uint32_t get_last_instanced_batch_count() const noexcept
    {
        return m_lastInstancedBatchCount;
    }
    inline uint64_t get_mesh_memory_bytes() const noexcept
    {
        return m_meshMemoryBytes;
    }
    inline uint64_t get_texture_memory_bytes() const noexcept
    {
        return m_textureMemoryBytes;
    }
    inline uint64_t get_instance_memory_bytes() const noexcept
    {
        return m_instanceBufferMemoryBytes;
    }
    inline uint64_t get_upload_staging_memory_bytes() const noexcept
    {
        return m_uploadStagingMemoryBytes;
    }
    inline uint64_t get_scene_target_memory_bytes() const noexcept
    {
        return m_sceneColorMemoryBytes + m_sceneDepthMemoryBytes + m_msaaColorMemoryBytes;
    }
    inline uint64_t get_tracked_device_local_memory_bytes() const noexcept
    {
        return m_meshMemoryBytes + m_textureMemoryBytes + get_scene_target_memory_bytes();
    }
    inline uint64_t get_tracked_host_visible_memory_bytes() const noexcept
    {
        return m_instanceBufferMemoryBytes + m_uploadStagingMemoryBytes;
    }
    inline uint64_t get_total_tracked_memory_bytes() const noexcept
    {
        return get_tracked_device_local_memory_bytes() + get_tracked_host_visible_memory_bytes();
    }
    inline DeviceLocalMemoryBudget get_device_local_memory_budget() const noexcept
    {
        return m_vulkanDevice.get_device_local_memory_budget();
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
    void set_renderables(const std::vector<Renderable>& renderables) noexcept override;

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
    void set_msaa_samples(std::int32_t samples) noexcept override;
    std::int32_t get_msaa_samples() const noexcept override;
    void set_render_scale(float scale) noexcept override;
    float get_render_scale() const noexcept override;

    // --- Shader management (IRenderer overrides) ---
    void set_shader_paths(const std::filesystem::path& vertPath, const std::filesystem::path& fragPath) override;
    Result<> recompile_shaders() override;

  private:
    Result<> create_command_buffers();
    Result<> record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept;
    Result<> create_sync_objects();
    Result<> recreate_swap_chain();
    Result<> ensure_instance_buffer_capacity(std::size_t requiredInstances);
    Result<> ensure_upload_staging_capacity(VkDeviceSize requiredSize);
    void cleanup_upload_staging_buffer() noexcept;
    Result<> create_sampler();
    Result<> create_default_textures();
    Result<> create_descriptor_pool();
    Result<> create_default_material();
    void destroy_meshes() noexcept;
    void destroy_textures() noexcept;
    void destroy_material_pool() noexcept;

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

    struct InstanceBatch
    {
        uint32_t meshIndex{0};
        uint32_t materialIndex{0};
        uint32_t firstInstance{0};
        uint32_t instanceCount{0};
    };

    struct InstanceData
    {
        DirectX::XMFLOAT4X4 mvp{};
        DirectX::XMFLOAT4X4 model{};
    };

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
    uint32_t m_totalTriangleCountCached{0};
    uint32_t m_lastVisibleRenderableCount{0};
    uint32_t m_lastCulledRenderableCount{0};
    uint32_t m_lastDrawCallCount{0};
    uint32_t m_lastInstancedBatchCount{0};
    uint64_t m_meshMemoryBytes{0};
    uint64_t m_textureMemoryBytes{0};
    uint64_t m_instanceBufferMemoryBytes{0};
    uint64_t m_uploadStagingMemoryBytes{0};
    std::unordered_map<std::string, uint32_t> m_meshLookup{};

    std::vector<GpuTexture> m_textures{};
    std::unordered_map<std::string, uint32_t> m_textureLookup{};
    VkSampler m_sampler{VK_NULL_HANDLE};
    uint32_t m_defaultAlbedoTextureIndex{};
    uint32_t m_defaultNormalTextureIndex{};

    std::vector<GpuMaterial> m_materials{};
    std::unordered_map<uint64_t, uint32_t> m_materialLookup{};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    uint32_t m_defaultMaterialIndex{};

    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_instanceBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_instanceBufferMemories{};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> m_instanceBufferMappings{};
    std::array<VkDeviceSize, MAX_FRAMES_IN_FLIGHT> m_instanceBufferAllocatedBytes{};
    std::size_t m_instanceBufferCapacity{0};
    std::vector<InstanceData> m_instanceDataScratch{};
    std::vector<InstanceBatch> m_instanceBatchesScratch{};

    VkBuffer m_uploadStagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_uploadStagingBufferMemory{VK_NULL_HANDLE};
    void* m_uploadStagingMapped{nullptr};
    VkDeviceSize m_uploadStagingCapacity{0};

    DirectX::XMFLOAT4X4 m_viewMatrix{};
    DirectX::XMFLOAT4X4 m_projMatrix{};

    // --- Offscreen scene render target ---
    VkRenderPass m_sceneRenderPass{VK_NULL_HANDLE};
    VkFramebuffer m_sceneFramebuffer{VK_NULL_HANDLE};

    // Resolve/final color (always 1x sample) — blit source
    VkImage m_sceneColorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_sceneColorMemory{VK_NULL_HANDLE};
    VkImageView m_sceneColorView{VK_NULL_HANDLE};
    uint64_t m_sceneColorMemoryBytes{0};

    // Depth (matches MSAA sample count)
    VkImage m_sceneDepthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_sceneDepthMemory{VK_NULL_HANDLE};
    VkImageView m_sceneDepthView{VK_NULL_HANDLE};
    uint64_t m_sceneDepthMemoryBytes{0};

    // MSAA color (only when msaaSamples > 1)
    VkImage m_msaaColorImage{VK_NULL_HANDLE};
    VkDeviceMemory m_msaaColorMemory{VK_NULL_HANDLE};
    VkImageView m_msaaColorView{VK_NULL_HANDLE};
    uint64_t m_msaaColorMemoryBytes{0};

    uint32_t m_sceneRenderWidth{0};
    uint32_t m_sceneRenderHeight{0};

    // --- Settings ---
    float m_renderScale{1.0f};
    VkSampleCountFlagBits m_msaaSamples{VK_SAMPLE_COUNT_1_BIT};
    bool m_vsyncEnabled{false}; // Default: auto (mailbox preferred)

    // --- Shader paths and cached SPIR-V ---
    std::filesystem::path m_vertShaderPath{"Resources/shader.vert"};
    std::filesystem::path m_fragShaderPath{"Resources/shader.frag"};
    std::vector<uint32_t> m_vertSpirv{};
    std::vector<uint32_t> m_fragSpirv{};
};

NOC_RESTORE_DLL_WARNINGS
