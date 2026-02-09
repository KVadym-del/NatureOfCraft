#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "VulkanDevice.hpp"

#include <vector>

#include <vulkan/vulkan.h>

NOC_SUPPRESS_DLL_WARNINGS

/// Owns the Vulkan graphics pipeline and pipeline layout.
class NOC_EXPORT VulkanPipeline
{
  public:
    explicit VulkanPipeline(VulkanDevice& device) : m_device(device)
    {}
    ~VulkanPipeline();

    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    /// Creates the graphics pipeline using the given render pass.
    Result<> initialize(VkRenderPass renderPass);

    /// Destroys pipeline and pipeline layout.
    void cleanup() noexcept;

    // --- Getters ---
    inline VkPipeline get_pipeline() const noexcept
    {
        return m_graphicsPipeline;
    }
    inline VkPipelineLayout get_pipeline_layout() const noexcept
    {
        return m_pipelineLayout;
    }

  private:
    Result<VkShaderModule> create_shader_module(const std::vector<char>& code) noexcept;

    // --- Members ---
    VulkanDevice& m_device;

    VkPipeline m_graphicsPipeline{};
    VkPipelineLayout m_pipelineLayout{};
};

NOC_RESTORE_DLL_WARNINGS
