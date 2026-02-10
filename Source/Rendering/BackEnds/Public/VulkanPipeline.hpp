#pragma once
#include "../../../Core/Public/Expected.hpp"
#include "VulkanDevice.hpp"

#include <cstdint>
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

    /// Creates the graphics pipeline using pre-compiled SPIR-V bytecode.
    /// @param renderPass   The Vulkan render pass this pipeline will be used with.
    /// @param msaaSamples  MSAA sample count for multisampling state.
    /// @param vertSpirv    Compiled vertex shader SPIR-V (uint32_t words).
    /// @param fragSpirv    Compiled fragment shader SPIR-V (uint32_t words).
    Result<> initialize(VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples,
                        const std::vector<uint32_t>& vertSpirv, const std::vector<uint32_t>& fragSpirv);

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
    inline VkDescriptorSetLayout get_descriptor_set_layout() const noexcept
    {
        return m_descriptorSetLayout;
    }

  private:
    Result<VkShaderModule> create_shader_module(const std::vector<uint32_t>& spirv) noexcept;

    // --- Members ---
    VulkanDevice& m_device;

    VkPipeline m_graphicsPipeline{};
    VkPipelineLayout m_pipelineLayout{};
    VkDescriptorSetLayout m_descriptorSetLayout{};
};

NOC_RESTORE_DLL_WARNINGS
