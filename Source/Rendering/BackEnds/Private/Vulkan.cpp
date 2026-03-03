#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Public/Vulkan.hpp"
#include "../../../Assets/Public/MeshData.hpp"
#include "../../../Assets/Public/TextureData.hpp"
#include "../../Public/Mesh.hpp"
#include "../../Public/ShaderCompiler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <tuple>
#include <vector>

#include <GLFW/glfw3.h>

#include <imgui_impl_vulkan.h>

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <fmt/core.h>
#include "../../../ThirdParty/NIS/NIS_Config.h"
using namespace DirectX;

/// Initialization

Result<> Vulkan::initialize() noexcept
{
    if (auto result = m_vulkanDevice.initialize(); !result)
        return result;
    if (auto result = m_swapchain.initialize(); !result)
        return result;

    // Compute scene render size and create offscreen resources
    compute_scene_render_size();
    if (auto result = create_scene_render_pass(); !result)
        return result;
    if (auto result = create_scene_render_target(); !result)
        return result;

    // Compile shaders from source (or use cached .spv)
    {
        auto vertSpvPath = ShaderCompiler::get_spv_path(m_vertShaderPath);
        auto vertResult = ShaderCompiler::compile_or_cache(m_vertShaderPath, vertSpvPath);
        if (!vertResult)
            return make_error(vertResult.error());
        m_vertSpirv = std::move(vertResult.value());

        auto fragSpvPath = ShaderCompiler::get_spv_path(m_fragShaderPath);
        auto fragResult = ShaderCompiler::compile_or_cache(m_fragShaderPath, fragSpvPath);
        if (!fragResult)
            return make_error(fragResult.error());
        m_fragSpirv = std::move(fragResult.value());
    }

    {
        MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
        if (auto result = m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv); !result)
            return result;
    }

    if (auto result = create_sampler(); !result)
        return result;
    if (auto result = create_default_textures(); !result)
        return result;
    if (auto result = create_descriptor_pool(); !result)
        return result;
    if (auto result = create_default_material(); !result)
        return result;
    if (auto result = create_command_buffers(); !result)
        return result;
    if (auto result = create_sync_objects(); !result)
        return result;

    return {};
}

/// Public methods

void Vulkan::set_renderables(const std::vector<Renderable>& renderables) noexcept
{
    m_renderables = renderables;
    std::sort(m_renderables.begin(), m_renderables.end(), [](const Renderable& lhs, const Renderable& rhs) {
        return std::tie(lhs.materialIndex, lhs.meshIndex) < std::tie(rhs.materialIndex, rhs.meshIndex);
    });

    std::uint32_t totalTriangles = 0;
    for (const auto& renderable : m_renderables)
    {
        if (renderable.meshIndex < m_meshes.size())
            totalTriangles += m_meshes[renderable.meshIndex].indexCount / 3;
    }
    m_totalTriangleCountCached = totalTriangles;
}

MeshBounds Vulkan::get_mesh_bounds(std::uint32_t meshIndex) const noexcept
{
    if (meshIndex >= m_meshes.size())
        return {};

    const auto& mesh = m_meshes[meshIndex];
    MeshBounds bounds{};
    bounds.min = mesh.boundsMin;
    bounds.max = mesh.boundsMax;
    bounds.center = mesh.boundsCenter;
    bounds.radius = mesh.boundsRadius;
    bounds.valid = mesh.boundsRadius > 0.0f;
    return bounds;
}

Result<> Vulkan::draw_frame() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();

    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], true, UINT64_MAX);

    if (m_framebufferResized)
    {
        m_framebufferResized = false;
        if (auto recreateResult = recreate_swap_chain(); !recreateResult)
            return recreateResult;
        // Scene render targets were rebuilt — the ImGui draw data for this frame
        // still references the old (now-destroyed) descriptor/image view.
        // Skip this frame entirely so the next iteration builds ImGui with the
        // correct, freshly-created resources.
        return {};
    }

    if (m_sceneFramebuffer == nullptr || m_sceneRenderPass == nullptr || m_sceneColorImage == nullptr)
    {
        if (auto recreateResult = recreate_swap_chain(); !recreateResult)
            return recreateResult;
        return {};
    }

    std::uint32_t imageIndex{};
    VkResult result = vkAcquireNextImageKHR(
        device,
        m_swapchain.get_swapchain(),
        std::numeric_limits<uint64_t>::max(),
        m_imageAvailableSemaphores[m_currentFrame],
        nullptr,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return recreate_swap_chain();
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return make_error("Failed to acquire swap chain image", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
    {
        return make_error("Failed to reset fence", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0) != VK_SUCCESS)
    {
        return make_error("Failed to reset command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    auto recordResult = record_command_buffer(m_commandBuffers[m_currentFrame], imageIndex);
    if (!recordResult)
        return make_error(recordResult.error());

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[]{m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[]{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[]{m_renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    const VkResult submitResult = vkQueueSubmit(
        m_vulkanDevice.get_graphics_queue(),
        1,
        &submitInfo,
        m_inFlightFences[m_currentFrame]
    );
    if (submitResult != VK_SUCCESS)
    {
        fmt::print("Warning: vkQueueSubmit failed with VkResult={}\n", static_cast<std::int32_t>(submitResult));
        if (submitResult != VK_ERROR_DEVICE_LOST)
        {
            if (auto recover = recreate_swap_chain(); recover)
                return {};
        }
        return make_error("Failed to submit draw command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[]{m_swapchain.get_swapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_vulkanDevice.get_present_queue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        return recreate_swap_chain();
    }
    else if (result != VK_SUCCESS)
    {
        return make_error("Failed to present swap chain image", ErrorCode::VulkanDrawFrameFailed);
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    return {};
}

void Vulkan::wait_idle() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (device)
        vkDeviceWaitIdle(device);
}

Result<> Vulkan::clear_scene_content() noexcept
{
    wait_idle();

    m_renderables.clear();
    m_totalTriangleCountCached = 0;
    m_lastVisibleRenderableCount = 0;
    m_lastCulledRenderableCount = 0;
    m_lastDrawCallCount = 0;
    m_lastInstancedBatchCount = 0;

    destroy_meshes();
    destroy_textures();
    destroy_material_pool();

    if (auto result = create_default_textures(); !result)
        return result;
    if (auto result = create_descriptor_pool(); !result)
        return result;
    if (auto result = create_default_material(); !result)
        return result;

    return {};
}

void Vulkan::destroy_meshes() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
        return;

    for (auto& mesh : m_meshes)
    {
        if (mesh.indexBuffer != nullptr)
            vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
        if (mesh.indexBufferMemory != nullptr)
            vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
        if (mesh.vertexBuffer != nullptr)
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
        if (mesh.vertexBufferMemory != nullptr)
            vkFreeMemory(device, mesh.vertexBufferMemory, nullptr);
    }
    m_meshes.clear();
    m_meshLookup.clear();
    m_meshMemoryBytes = 0;
}

void Vulkan::destroy_textures() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
        return;

    for (auto& tex : m_textures)
    {
        if (tex.imageView != nullptr)
            vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image != nullptr)
            vkDestroyImage(device, tex.image, nullptr);
        if (tex.memory != nullptr)
            vkFreeMemory(device, tex.memory, nullptr);
    }

    m_textures.clear();
    m_textureLookup.clear();
    m_textureMemoryBytes = 0;
    m_defaultAlbedoTextureIndex = 0;
    m_defaultNormalTextureIndex = 0;
}

void Vulkan::destroy_material_pool() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
        return;

    if (m_descriptorPool != nullptr)
    {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
    }
    m_materials.clear();
    m_materialLookup.clear();
    m_defaultMaterialIndex = 0;
}

void Vulkan::cleanup() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();

    if (device)
        vkDeviceWaitIdle(device);

    destroy_meshes();
    destroy_textures();
    destroy_material_pool();

    // Destroy sampler
    if (m_sampler != nullptr)
    {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = nullptr;
    }

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_instanceBufferMappings[i] != nullptr && m_instanceBufferMemories[i] != nullptr)
        {
            vkUnmapMemory(device, m_instanceBufferMemories[i]);
            m_instanceBufferMappings[i] = nullptr;
        }
        if (m_instanceBuffers[i] != nullptr)
        {
            vkDestroyBuffer(device, m_instanceBuffers[i], nullptr);
            m_instanceBuffers[i] = nullptr;
        }
        if (m_instanceBufferMemories[i] != nullptr)
        {
            vkFreeMemory(device, m_instanceBufferMemories[i], nullptr);
            m_instanceBufferMemories[i] = nullptr;
        }
        m_instanceBufferAllocatedBytes[i] = 0;
    }
    m_instanceBufferCapacity = 0;
    m_instanceBufferMemoryBytes = 0;

    cleanup_upload_staging_buffer();

    // Destroy sync objects
    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_imageAvailableSemaphores.size() > i && m_imageAvailableSemaphores[i])
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
        if (m_inFlightFences.size() > i && m_inFlightFences[i])
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_inFlightFences.clear();

    for (std::size_t i = 0; i < m_renderFinishedSemaphores.size(); i++)
    {
        if (m_renderFinishedSemaphores[i])
            vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_renderFinishedSemaphores.clear();
    m_imagesInFlight.clear();

    if (!m_commandBuffers.empty() && m_vulkanDevice.get_command_pool() != nullptr)
    {
        vkFreeCommandBuffers(device, m_vulkanDevice.get_command_pool(), static_cast<uint32_t>(m_commandBuffers.size()),
                             m_commandBuffers.data());
        m_commandBuffers.clear();
    }

    // Destroy offscreen resources
    cleanup_nis_resources();
    cleanup_scene_render_target();
    cleanup_scene_render_pass();

    // Sub-components clean up
    m_pipeline.cleanup();
    m_pipeline.release_cache();
    m_swapchain.cleanup();
    m_vulkanDevice.cleanup();
}

/// Private methods

Result<> Vulkan::create_command_buffers()
{
    VkDevice device = m_vulkanDevice.get_device();

    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_vulkanDevice.get_command_pool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<std::uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
    {
        return make_error("Failed to allocate command buffer", ErrorCode::VulkanCommandBufferAllocationFailed);
    }

    return {};
}

Result<> Vulkan::ensure_upload_staging_capacity(VkDeviceSize requiredSize)
{
    if (requiredSize <= m_uploadStagingCapacity && m_uploadStagingBuffer != nullptr && m_uploadStagingMapped)
        return {};

    VkDevice device = m_vulkanDevice.get_device();

    // Grow geometrically to avoid frequent reallocations.
    VkDeviceSize newCapacity = std::max<VkDeviceSize>(requiredSize, 1024 * 1024);
    if (m_uploadStagingCapacity > 0)
    {
        while (newCapacity < requiredSize)
            newCapacity *= 2;

        newCapacity = std::max(newCapacity, m_uploadStagingCapacity * 2);
    }

    cleanup_upload_staging_buffer();

    VkDeviceSize allocatedBytes{};
    auto createResult = m_vulkanDevice.create_buffer(
        newCapacity,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_uploadStagingBuffer,
        m_uploadStagingBufferMemory,
        &allocatedBytes
    );
    if (!createResult)
        return make_error(createResult.error());

    if (vkMapMemory(device, m_uploadStagingBufferMemory, 0, newCapacity, 0, &m_uploadStagingMapped) != VK_SUCCESS)
    {
        cleanup_upload_staging_buffer();
        return make_error("Failed to map upload staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }

    m_uploadStagingCapacity = newCapacity;
    m_uploadStagingMemoryBytes = static_cast<std::uint64_t>(allocatedBytes);
    return {};
}

void Vulkan::cleanup_upload_staging_buffer() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
    {
        m_uploadStagingBuffer = nullptr;
        m_uploadStagingBufferMemory = nullptr;
        m_uploadStagingMapped = nullptr;
        m_uploadStagingCapacity = 0;
        m_uploadStagingMemoryBytes = 0;
        return;
    }

    if (m_uploadStagingMapped != nullptr && m_uploadStagingBufferMemory != nullptr)
    {
        vkUnmapMemory(device, m_uploadStagingBufferMemory);
        m_uploadStagingMapped = nullptr;
    }
    if (m_uploadStagingBuffer != nullptr)
    {
        vkDestroyBuffer(device, m_uploadStagingBuffer, nullptr);
        m_uploadStagingBuffer = nullptr;
    }
    if (m_uploadStagingBufferMemory != nullptr)
    {
        vkFreeMemory(device, m_uploadStagingBufferMemory, nullptr);
        m_uploadStagingBufferMemory = nullptr;
    }

    m_uploadStagingCapacity = 0;
    m_uploadStagingMemoryBytes = 0;
}

Result<> Vulkan::ensure_instance_buffer_capacity(std::size_t requiredInstances)
{
    if (requiredInstances <= m_instanceBufferCapacity)
        return {};

    std::size_t newCapacity = std::max<std::size_t>(requiredInstances, 256);
    if (m_instanceBufferCapacity > 0)
        newCapacity = std::max(newCapacity, m_instanceBufferCapacity * 2);

    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(sizeof(InstanceData) * newCapacity);
    std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> newBuffers{};
    std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> newMemories{};
    std::array<void*, MAX_FRAMES_IN_FLIGHT> newMappings{};
    std::array<VkDeviceSize, MAX_FRAMES_IN_FLIGHT> newAllocatedBytes{};

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto result = m_vulkanDevice.create_buffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            newBuffers[i],
            newMemories[i],
            &newAllocatedBytes[i]
        );
        if (!result)
        {
            VkDevice device = m_vulkanDevice.get_device();
            for (size_t j = 0; j <= i; ++j)
            {
                if (newMappings[j] != nullptr && newMemories[j] != nullptr)
                    vkUnmapMemory(device, newMemories[j]);
                if (newBuffers[j] != nullptr)
                    vkDestroyBuffer(device, newBuffers[j], nullptr);
                if (newMemories[j] != nullptr)
                    vkFreeMemory(device, newMemories[j], nullptr);
            }
            return make_error(result.error());
        }

        if (vkMapMemory(m_vulkanDevice.get_device(), newMemories[i], 0, bufferSize, 0, &newMappings[i]) != VK_SUCCESS)
        {
            VkDevice device = m_vulkanDevice.get_device();
            for (std::size_t j = 0; j <= i; ++j)
            {
                if (newMappings[j] != nullptr && newMemories[j] != nullptr)
                    vkUnmapMemory(device, newMemories[j]);
                if (newBuffers[j] != nullptr)
                    vkDestroyBuffer(device, newBuffers[j], nullptr);
                if (newMemories[j] != nullptr)
                    vkFreeMemory(device, newMemories[j], nullptr);
            }
            return make_error("Failed to map instance buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
        }
    }

    VkDevice device = m_vulkanDevice.get_device();
    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_instanceBufferMappings[i] != nullptr && m_instanceBufferMemories[i] != nullptr)
            vkUnmapMemory(device, m_instanceBufferMemories[i]);
        if (m_instanceBuffers[i] != nullptr)
            vkDestroyBuffer(device, m_instanceBuffers[i], nullptr);
        if (m_instanceBufferMemories[i] != nullptr)
            vkFreeMemory(device, m_instanceBufferMemories[i], nullptr);
        m_instanceBuffers[i] = newBuffers[i];
        m_instanceBufferMemories[i] = newMemories[i];
        m_instanceBufferMappings[i] = newMappings[i];
        m_instanceBufferAllocatedBytes[i] = newAllocatedBytes[i];
    }

    m_instanceBufferCapacity = newCapacity;
    m_instanceBufferMemoryBytes = 0;
    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        m_instanceBufferMemoryBytes += static_cast<std::uint64_t>(m_instanceBufferAllocatedBytes[i]);
    return {};
}

Result<> Vulkan::record_command_buffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        return make_error("Failed to begin recording command buffer", ErrorCode::VulkanCommandBufferRecordingFailed);
    }

    // Scene render pass (offscreen)
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_sceneRenderPass;
        renderPassInfo.framebuffer = m_sceneFramebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {m_sceneRenderWidth, m_sceneRenderHeight};

        std::array<VkClearValue, 3> clearValues{};
        std::uint32_t clearValueCount{2};
        if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
        {
            // Attachments: 0=MSAA color, 1=MSAA depth, 2=resolve color
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
            clearValues[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValueCount = 3;
        }
        else
        {
            // Attachments: 0=color, 1=depth
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
        }

        renderPassInfo.clearValueCount = clearValueCount;
        renderPassInfo.pClearValues = clearValues.data();

        // Use view/projection matrices set externally by the camera
        XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
        XMMATRIX proj = XMLoadFloat4x4(&m_projMatrix);
        XMMATRIX viewProj = XMMatrixMultiply(view, proj);

        // Build visible instances and instancing batches.
        m_instanceDataScratch.clear();
        m_instanceBatchesScratch.clear();
        m_instanceDataScratch.reserve(m_renderables.size());
        m_instanceBatchesScratch.reserve(m_renderables.size());

        // The renderer stores a Vulkan-flipped projection matrix (Y *= -1).
        // DirectXCollision frustum extraction expects a regular projection matrix.
        // Remove the flip for culling to avoid rejecting everything.
        const XMMATRIX vulkanFlip = XMMatrixScaling(1.0f, -1.0f, 1.0f);
        const XMMATRIX cullProj = XMMatrixMultiply(proj, vulkanFlip);

        BoundingFrustum viewFrustum{};
        BoundingFrustum::CreateFromMatrix(viewFrustum, cullProj, true);

        std::uint32_t culledRenderables{};
        for (const auto& renderable : m_renderables)
        {
            if (renderable.meshIndex >= m_meshes.size())
                continue;

            const auto& mesh = m_meshes[renderable.meshIndex];
            if (mesh.vertexBuffer == nullptr || mesh.indexBuffer == nullptr)
                continue;

            XMMATRIX world = XMLoadFloat4x4(&renderable.worldMatrix);
            XMVECTOR center = XMVectorSet(mesh.boundsCenter.x, mesh.boundsCenter.y, mesh.boundsCenter.z, 1.0f);
            XMVECTOR centerWorld = XMVector3TransformCoord(center, world);
            XMVECTOR centerView = XMVector3TransformCoord(centerWorld, view);

            const float scaleX = XMVectorGetX(XMVector3Length(world.r[0]));
            const float scaleY = XMVectorGetX(XMVector3Length(world.r[1]));
            const float scaleZ = XMVectorGetX(XMVector3Length(world.r[2]));
            const float maxScale = std::max(scaleX, std::max(scaleY, scaleZ));

            BoundingSphere sphere{};
            XMStoreFloat3(&sphere.Center, centerView);
            sphere.Radius = mesh.boundsRadius * maxScale;

            if (viewFrustum.Contains(sphere) == DISJOINT)
            {
                ++culledRenderables;
                continue;
            }

            std::uint32_t materialIndex = renderable.materialIndex;
            if (materialIndex >= m_materials.size())
                materialIndex = m_defaultMaterialIndex;

            XMMATRIX mvp = XMMatrixMultiply(world, viewProj);
            InstanceData instanceData{};
            XMStoreFloat4x4(&instanceData.mvp, mvp);
            instanceData.model = renderable.worldMatrix;
            instanceData.glow = {
                renderable.glowColor.x,
                renderable.glowColor.y,
                renderable.glowColor.z,
                std::max(0.0f, renderable.glowIntensity),
            };

            const std::uint32_t firstInstance = static_cast<std::uint32_t>(m_instanceDataScratch.size());
            m_instanceDataScratch.push_back(instanceData);

            if (!m_instanceBatchesScratch.empty())
            {
                auto& lastBatch = m_instanceBatchesScratch.back();
                if (lastBatch.meshIndex == renderable.meshIndex && lastBatch.materialIndex == materialIndex)
                {
                    ++lastBatch.instanceCount;
                    continue;
                }
            }

            m_instanceBatchesScratch.push_back(
                InstanceBatch{
                    renderable.meshIndex,
                    materialIndex,
                    firstInstance,
                    1
                }
            );
        }

        m_lastVisibleRenderableCount = static_cast<std::uint32_t>(m_instanceDataScratch.size());
        m_lastCulledRenderableCount = culledRenderables;
        m_lastInstancedBatchCount = static_cast<std::uint32_t>(m_instanceBatchesScratch.size());
        m_lastDrawCallCount = 0;

        if (!m_instanceDataScratch.empty())
        {
            if (auto result = ensure_instance_buffer_capacity(m_instanceDataScratch.size()); !result)
                return result;

            VkDeviceSize dataSize = static_cast<VkDeviceSize>(sizeof(InstanceData) * m_instanceDataScratch.size());
            void* mapped = m_instanceBufferMappings[m_currentFrame];
            if (mapped == nullptr)
            {
                return make_error("Instance buffer is not mapped", ErrorCode::VulkanMemoryAllocationFailed);
            }
            std::memcpy(mapped, m_instanceDataScratch.data(), static_cast<size_t>(dataSize));
        }

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.get_pipeline());

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_sceneRenderWidth);
        viewport.height = static_cast<float>(m_sceneRenderHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {m_sceneRenderWidth, m_sceneRenderHeight};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkPipelineLayout pipelineLayout = m_pipeline.get_pipeline_layout();

        for (const auto& batch : m_instanceBatchesScratch)
        {
            if (batch.meshIndex >= m_meshes.size())
                continue;

            const auto& mesh = m_meshes[batch.meshIndex];
            if (mesh.vertexBuffer == nullptr || mesh.indexBuffer == nullptr)
                continue;

            // Bind material descriptor set
            std::uint32_t matIdx = batch.materialIndex;
            if (matIdx < m_materials.size())
            {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,
                    0,
                    1,
                    &m_materials[matIdx].descriptorSet,
                    0,
                    nullptr
                );
            }
            else if (!m_materials.empty())
            {
                // Fallback to default material (index 0)
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout,
                    0,
                    1,
                    &m_materials[m_defaultMaterialIndex].descriptorSet,
                    0,
                    nullptr
                );
            }

            if (m_instanceBuffers[m_currentFrame] == nullptr)
                continue;

            std::array<VkBuffer, 2> vertexBuffers{mesh.vertexBuffer, m_instanceBuffers[m_currentFrame]};
            std::array<VkDeviceSize, 2> offsets{0, 0};
            vkCmdBindVertexBuffers(
                commandBuffer,
                0,
                static_cast<std::uint32_t>(vertexBuffers.size()),
                vertexBuffers.data(),
                offsets.data()
            );

            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, batch.instanceCount, 0, 0, batch.firstInstance);
            ++m_lastDrawCallCount;
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    // Scene color image is sampled in the ImGui viewport pass.
    // When NIS is enabled, run compute pass; otherwise add a visibility barrier.
    if (m_nisEnabled && m_nisComputePipeline != nullptr)
    {
        dispatch_nis_pass(commandBuffer);
    }
    else
    {
        // Standard barrier: color output writes -> fragment shader reads
        VkImageMemoryBarrier sceneReadBarrier{};
        sceneReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sceneReadBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sceneReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        sceneReadBarrier.image = m_sceneColorImage;
        sceneReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sceneReadBarrier.subresourceRange.baseMipLevel = 0;
        sceneReadBarrier.subresourceRange.levelCount = 1;
        sceneReadBarrier.subresourceRange.baseArrayLayer = 0;
        sceneReadBarrier.subresourceRange.layerCount = 1;
        sceneReadBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sceneReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &sceneReadBarrier
        );
    }

    // UI render pass (swapchain)
    {
        VkExtent2D swapExtent = m_swapchain.get_extent();

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_swapchain.get_ui_render_pass();
        renderPassInfo.framebuffer = m_swapchain.get_ui_framebuffer(imageIndex);
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapExtent;
        VkClearValue uiClearColor{};
        uiClearColor.color = {{0.06f, 0.06f, 0.07f, 1.0f}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &uiClearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport/scissor to window resolution for UI
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapExtent.width);
        viewport.height = static_cast<float>(swapExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        if (get_ui_render_callback())
        {
            get_ui_render_callback()(commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        return make_error("Failed to record command buffer", ErrorCode::VulkanCommandBufferRecordingFailed);
    }

    return {};
}

Result<> Vulkan::create_sync_objects()
{
    VkDevice device = m_vulkanDevice.get_device();
    std::uint32_t imageCount = m_swapchain.get_image_count();

    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    m_renderFinishedSemaphores.resize(imageCount);
    m_imagesInFlight.resize(imageCount, nullptr);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    auto rollback_sync_objects = [&]() {
        for (VkSemaphore semaphore : m_imageAvailableSemaphores)
        {
            if (semaphore != nullptr)
                vkDestroySemaphore(device, semaphore, nullptr);
        }
        for (VkFence fence : m_inFlightFences)
        {
            if (fence != nullptr)
                vkDestroyFence(device, fence, nullptr);
        }
        for (VkSemaphore semaphore : m_renderFinishedSemaphores)
        {
            if (semaphore != nullptr)
                vkDestroySemaphore(device, semaphore, nullptr);
        }
        m_imageAvailableSemaphores.clear();
        m_inFlightFences.clear();
        m_renderFinishedSemaphores.clear();
        m_imagesInFlight.clear();
    };

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS)
        {
            rollback_sync_objects();
            return make_error(
                "Failed to create synchronization objects for a frame",
                ErrorCode::VulkanSyncObjectsCreationFailed
            );
        }
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            rollback_sync_objects();
            return make_error(
                "Failed to create synchronization objects for a frame",
                ErrorCode::VulkanSyncObjectsCreationFailed
            );
        }
    }

    for (std::size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            rollback_sync_objects();
            return make_error("Failed to create render finished semaphore", ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    return {};
}

Result<> Vulkan::recreate_swap_chain()
{
    GLFWwindow* window = m_vulkanDevice.get_window();
    VkDevice device = m_vulkanDevice.get_device();

    std::int32_t width{};
    std::int32_t height{};
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    if (auto result = m_swapchain.recreate(); !result)
        return result;

    // Recreate offscreen scene render target
    cleanup_scene_render_target();
    compute_scene_render_size();
    if (auto result = create_scene_render_target(); !result)
        return result;

    // Recreate per-image sync objects for the new swapchain.
    // Build the new set first, then atomically swap and destroy old ones.
    std::uint32_t imageCount = m_swapchain.get_image_count();
    std::vector<VkSemaphore> newRenderFinishedSemaphores(imageCount, nullptr);
    std::vector<VkFence> newImagesInFlight(imageCount, nullptr);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (std::size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &newRenderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            for (VkSemaphore semaphore : newRenderFinishedSemaphores)
            {
                if (semaphore != nullptr)
                    vkDestroySemaphore(device, semaphore, nullptr);
            }
            return make_error(
                "Failed to recreate render finished semaphore",
                ErrorCode::VulkanSyncObjectsCreationFailed
            );
        }
    }

    for (VkSemaphore semaphore : m_renderFinishedSemaphores)
    {
        if (semaphore != nullptr)
            vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores = std::move(newRenderFinishedSemaphores);
    m_imagesInFlight = std::move(newImagesInFlight);

    if (m_nisEnabled)
    {
        cleanup_nis_resources();
        if (auto nisResult = create_nis_resources(); !nisResult)
            fmt::print("Warning: failed to recreate NIS resources: {}\n", nisResult.error().message);
    }

    // Notify listeners about the swapchain recreation.
    if (m_swapchainRecreatedCallback)
    {
        m_swapchainRecreatedCallback();
    }

    return {};
}

Result<uint32_t> Vulkan::upload_mesh(const MeshData& meshData)
{
    if (meshData.vertices.empty())
        return make_error("MeshData has no vertices", ErrorCode::AssetInvalidData);

    std::string meshKey{};
    if (!meshData.sourcePath.empty())
    {
        meshKey = meshData.sourcePath.generic_string();
        meshKey.push_back('|');
        meshKey += meshData.name;
        if (const auto it = m_meshLookup.find(meshKey); it != m_meshLookup.end())
            return it->second;
    }

    VkDevice device = m_vulkanDevice.get_device();
    Mesh mesh{};
    mesh.indexCount = static_cast<std::uint32_t>(meshData.indices.size());
    auto cleanup_mesh_gpu_buffers = [&]() {
        if (mesh.indexBuffer != nullptr)
        {
            vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
            mesh.indexBuffer = nullptr;
        }
        if (mesh.indexBufferMemory != nullptr)
        {
            vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
            mesh.indexBufferMemory = nullptr;
        }
        if (mesh.vertexBuffer != nullptr)
        {
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
            mesh.vertexBuffer = nullptr;
        }
        if (mesh.vertexBufferMemory != nullptr)
        {
            vkFreeMemory(device, mesh.vertexBufferMemory, nullptr);
            mesh.vertexBufferMemory = nullptr;
        }
    };

    // --- Vertex/index buffers ---
    const VkDeviceSize vertexBufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
    const VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();
    const VkDeviceSize indexSrcOffset = (vertexBufferSize + 3) & ~static_cast<VkDeviceSize>(3);
    const VkDeviceSize totalStagingSize = indexSrcOffset + indexBufferSize;
    mesh.vertexBufferBytes = vertexBufferSize;
    mesh.indexBufferBytes = indexBufferSize;
    VkDeviceSize vertexAllocationBytes{};
    VkDeviceSize indexAllocationBytes{};

    if (auto res = ensure_upload_staging_capacity(totalStagingSize); !res)
        return make_error(res.error());
    std::memcpy(m_uploadStagingMapped, meshData.vertices.data(), static_cast<std::size_t>(vertexBufferSize));
    std::memcpy(
        static_cast<std::byte*>(m_uploadStagingMapped) + indexSrcOffset,
        meshData.indices.data(),
        static_cast<std::size_t>(indexBufferSize)
    );

    if (auto res = m_vulkanDevice.create_buffer(
            vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mesh.vertexBuffer,
            mesh.vertexBufferMemory,
            &vertexAllocationBytes
    ); !res)
    {
        return make_error(res.error());
    }

    if (auto res = m_vulkanDevice.create_buffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        mesh.indexBuffer,
        mesh.indexBufferMemory,
        &indexAllocationBytes
    ); !res)
    {
        cleanup_mesh_gpu_buffers();
        return make_error(res.error());
    }

    auto commandBufferResult = m_vulkanDevice.begin_single_time_commands();
    if (!commandBufferResult)
    {
        cleanup_mesh_gpu_buffers();
        return make_error("Failed to begin upload command buffer", ErrorCode::VulkanCopyBufferFailed);
    }
    VkCommandBuffer commandBuffer = commandBufferResult.value();

    VkBufferCopy vertexCopy{};
    vertexCopy.srcOffset = 0;
    vertexCopy.dstOffset = 0;
    vertexCopy.size = vertexBufferSize;
    vkCmdCopyBuffer(commandBuffer, m_uploadStagingBuffer, mesh.vertexBuffer, 1, &vertexCopy);

    VkBufferCopy indexCopy{};
    indexCopy.srcOffset = indexSrcOffset;
    indexCopy.dstOffset = 0;
    indexCopy.size = indexBufferSize;
    vkCmdCopyBuffer(commandBuffer, m_uploadStagingBuffer, mesh.indexBuffer, 1, &indexCopy);

    if (auto endResult = m_vulkanDevice.end_single_time_commands(commandBuffer); !endResult)
    {
        cleanup_mesh_gpu_buffers();
        return make_error("Failed to submit mesh upload command buffer", ErrorCode::VulkanCopyBufferFailed);
    }

    mesh.boundsCenter = {
        (meshData.boundsMin.x + meshData.boundsMax.x) * 0.5f,
        (meshData.boundsMin.y + meshData.boundsMax.y) * 0.5f,
        (meshData.boundsMin.z + meshData.boundsMax.z) * 0.5f,
    };
    mesh.boundsMin = meshData.boundsMin;
    mesh.boundsMax = meshData.boundsMax;
    const float halfX = (meshData.boundsMax.x - meshData.boundsMin.x) * 0.5f;
    const float halfY = (meshData.boundsMax.y - meshData.boundsMin.y) * 0.5f;
    const float halfZ = (meshData.boundsMax.z - meshData.boundsMin.z) * 0.5f;
    mesh.boundsRadius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
    mesh.vertexBufferAllocationBytes = vertexAllocationBytes;
    mesh.indexBufferAllocationBytes = indexAllocationBytes;

    std::uint32_t meshIndex = static_cast<std::uint32_t>(m_meshes.size());
    m_meshes.push_back(mesh);
    m_meshMemoryBytes += static_cast<std::uint64_t>(mesh.vertexBufferAllocationBytes + mesh.indexBufferAllocationBytes);
    if (!meshKey.empty())
        m_meshLookup.emplace(std::move(meshKey), meshIndex);
    return meshIndex;
}

Result<> Vulkan::create_sampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_vulkanDevice.get_device(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
    {
        return make_error("Failed to create texture sampler", ErrorCode::VulkanSamplerCreationFailed);
    }

    return {};
}

Result<> Vulkan::create_default_textures()
{
    // Default albedo: 1x1 white pixel (255, 255, 255, 255)
    TextureData whiteTexture{};
    whiteTexture.name = "default_albedo";
    whiteTexture.width = 1;
    whiteTexture.height = 1;
    whiteTexture.channels = 4;
    whiteTexture.pixels = {255, 255, 255, 255};

    auto albedoResult = upload_texture(whiteTexture);
    if (!albedoResult)
        return make_error(albedoResult.error());
    m_defaultAlbedoTextureIndex = albedoResult.value();

    // Default normal: 1x1 flat normal (128, 128, 255, 255) => (0, 0, 1) in tangent space
    TextureData flatNormal{};
    flatNormal.name = "default_normal";
    flatNormal.width = 1;
    flatNormal.height = 1;
    flatNormal.channels = 4;
    flatNormal.pixels = {128, 128, 255, 255};

    auto normalResult = upload_texture(flatNormal);
    if (!normalResult)
        return make_error(normalResult.error());
    m_defaultNormalTextureIndex = normalResult.value();

    // Default roughness: 1x1 white pixel (roughness = 1.0)
    TextureData whiteRoughness{};
    whiteRoughness.name = "default_roughness";
    whiteRoughness.width = 1;
    whiteRoughness.height = 1;
    whiteRoughness.channels = 4;
    whiteRoughness.pixels = {255, 255, 255, 255};

    auto roughnessResult = upload_texture(whiteRoughness);
    if (!roughnessResult)
        return make_error(roughnessResult.error());
    m_defaultRoughnessTextureIndex = roughnessResult.value();

    // Default metallic: 1x1 black pixel (metallic = 0.0)
    TextureData blackMetallic{};
    blackMetallic.name = "default_metallic";
    blackMetallic.width = 1;
    blackMetallic.height = 1;
    blackMetallic.channels = 4;
    blackMetallic.pixels = {0, 0, 0, 255};

    auto metallicResult = upload_texture(blackMetallic);
    if (!metallicResult)
        return make_error(metallicResult.error());
    m_defaultMetallicTextureIndex = metallicResult.value();

    // Default AO: 1x1 white pixel (fully lit, no occlusion)
    TextureData whiteAO{};
    whiteAO.name = "default_ao";
    whiteAO.width = 1;
    whiteAO.height = 1;
    whiteAO.channels = 4;
    whiteAO.pixels = {255, 255, 255, 255};

    auto aoResult = upload_texture(whiteAO);
    if (!aoResult)
        return make_error(aoResult.error());
    m_defaultAOTextureIndex = aoResult.value();

    return {};
}

Result<std::uint32_t> Vulkan::upload_texture(const TextureData& textureData)
{
    if (textureData.pixels.empty() || textureData.width == 0 || textureData.height == 0)
        return make_error("TextureData has no pixel data", ErrorCode::AssetInvalidData);

    if (!textureData.sourcePath.empty())
    {
        const std::string textureKey = textureData.sourcePath.generic_string();
        if (const auto it = m_textureLookup.find(textureKey); it != m_textureLookup.end())
            return it->second;
    }

    VkDevice device = m_vulkanDevice.get_device();
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(textureData.width) * textureData.height * 4;

    if (auto res = ensure_upload_staging_capacity(imageSize); !res)
        return make_error(res.error());

    std::memcpy(m_uploadStagingMapped, textureData.pixels.data(), static_cast<size_t>(imageSize));

    GpuTexture texture{};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
    VkDeviceSize textureAllocationBytes{};

    if (auto res = m_vulkanDevice.create_image(
        textureData.width,
        textureData.height,
        format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        texture.image,
        texture.memory,
        VK_SAMPLE_COUNT_1_BIT,
        &textureAllocationBytes
    ); !res)
    {
        return make_error(res.error());
    }

    auto commandBufferResult = m_vulkanDevice.begin_single_time_commands();
    if (!commandBufferResult)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        return make_error("Failed to begin texture upload command buffer", ErrorCode::VulkanTextureUploadFailed);
    }
    VkCommandBuffer commandBuffer = commandBufferResult.value();

    VkImageMemoryBarrier toTransferBarrier{};
    toTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferBarrier.image = texture.image;
    toTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransferBarrier.subresourceRange.baseMipLevel = 0;
    toTransferBarrier.subresourceRange.levelCount = 1;
    toTransferBarrier.subresourceRange.baseArrayLayer = 0;
    toTransferBarrier.subresourceRange.layerCount = 1;
    toTransferBarrier.srcAccessMask = 0;
    toTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransferBarrier
    );

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {textureData.width, textureData.height, 1};
    vkCmdCopyBufferToImage(
        commandBuffer,
        m_uploadStagingBuffer,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );

    VkImageMemoryBarrier toShaderReadBarrier = toTransferBarrier;
    toShaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toShaderReadBarrier
    );

    if (auto endResult = m_vulkanDevice.end_single_time_commands(commandBuffer); !endResult)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        return make_error("Failed to submit texture upload command buffer", ErrorCode::VulkanTextureUploadFailed);
    }

    auto viewResult = m_vulkanDevice.create_image_view(texture.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!viewResult)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        return make_error(viewResult.error());
    }
    texture.imageView = viewResult.value();
    texture.width = textureData.width;
    texture.height = textureData.height;
    texture.allocationBytes = textureAllocationBytes;

    std::uint32_t textureIndex = static_cast<std::uint32_t>(m_textures.size());
    m_textures.push_back(texture);
    m_textureMemoryBytes += static_cast<std::uint64_t>(texture.allocationBytes);
    if (!textureData.sourcePath.empty())
        m_textureLookup.emplace(textureData.sourcePath.generic_string(), textureIndex);
    return textureIndex;
}


Result<> Vulkan::create_descriptor_pool()
{
    VkDevice device = m_vulkanDevice.get_device();

    // Pool sized for up to 256 materials, each needing 5 combined image samplers
    constexpr std::uint32_t maxMaterials{256};

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxMaterials * 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = maxMaterials;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        return make_error("Failed to create descriptor pool", ErrorCode::VulkanTextureUploadFailed);
    }

    return {};
}

Result<> Vulkan::create_default_material()
{
    auto result = upload_material(
        m_defaultAlbedoTextureIndex,
        m_defaultNormalTextureIndex,
        m_defaultRoughnessTextureIndex,
        m_defaultMetallicTextureIndex,
        m_defaultAOTextureIndex
    );
    if (!result)
        return make_error(result.error());
    m_defaultMaterialIndex = result.value();
    return {};
}

Result<std::uint32_t> Vulkan::upload_material(
    std::uint32_t albedoTextureIndex,
    std::uint32_t normalTextureIndex,
    std::uint32_t roughnessTextureIndex,
    std::uint32_t metallicTextureIndex,
    std::uint32_t aoTextureIndex
)
{
    if (albedoTextureIndex >= m_textures.size() ||
        normalTextureIndex >= m_textures.size() ||
        roughnessTextureIndex >= m_textures.size() ||
        metallicTextureIndex >= m_textures.size() ||
        aoTextureIndex >= m_textures.size())
        return make_error("Texture index out of range", ErrorCode::AssetInvalidData);

    std::size_t materialKey{};
    materialKey ^= std::hash<std::uint32_t>{}(albedoTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<std::uint32_t>{}(normalTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<std::uint32_t>{}(roughnessTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<std::uint32_t>{}(metallicTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<std::uint32_t>{}(aoTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    const std::uint64_t key64 = static_cast<std::uint64_t>(materialKey);
    if (const auto it = m_materialLookup.find(key64); it != m_materialLookup.end())
        return it->second;

    VkDevice device = m_vulkanDevice.get_device();

    VkDescriptorSetLayout layout = m_pipeline.get_descriptor_set_layout();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    GpuMaterial material{};
    if (vkAllocateDescriptorSets(device, &allocInfo, &material.descriptorSet) != VK_SUCCESS)
    {
        return make_error("Failed to allocate descriptor set for material", ErrorCode::VulkanTextureUploadFailed);
    }

    // Write descriptor set: binding 0=albedo, 1=normal, 2=roughness, 3=metallic, 4=AO
    const std::uint32_t texIndices[5] {
        albedoTextureIndex,
        normalTextureIndex,
        roughnessTextureIndex,
        metallicTextureIndex,
        aoTextureIndex 
    };
    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

    for (std::uint32_t i = 0; i < 5; ++i)
    {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = m_textures[texIndices[i]].imageView;
        imageInfos[i].sampler = m_sampler;

        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = material.descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

    std::uint32_t materialIndex = static_cast<std::uint32_t>(m_materials.size());
    m_materials.push_back(material);
    m_materialLookup.emplace(key64, materialIndex);
    return materialIndex;
}

void Vulkan::compute_scene_render_size()
{
    VkExtent2D swapExtent = m_swapchain.get_extent();
    m_sceneRenderWidth = std::max(1u, static_cast<std::uint32_t>(swapExtent.width * m_renderScale));
    m_sceneRenderHeight = std::max(1u, static_cast<std::uint32_t>(swapExtent.height * m_renderScale));
}

Result<VkFormat> Vulkan::find_depth_format()
{
    return find_supported_format(
        {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

Result<VkFormat> Vulkan::find_supported_format(
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
)
{
    VkPhysicalDevice physicalDevice = m_vulkanDevice.get_physical_device();

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }
    return make_error("Failed to find supported format", ErrorCode::VulkanFormatNotSupported);
}

VkSampleCountFlagBits Vulkan::get_max_usable_sample_count() const noexcept
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_vulkanDevice.get_physical_device(), &properties);

    VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_64_BIT)
        return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT)
        return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT)
        return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT)
        return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT)
        return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT)
        return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

Result<> Vulkan::create_scene_render_pass()
{
    VkDevice device = m_vulkanDevice.get_device();
    VkFormat colorFormat = m_swapchain.get_format();

    auto depthFormatResult = find_depth_format();
    if (!depthFormatResult)
        return make_error(depthFormatResult.error());
    VkFormat depthFormat = depthFormatResult.value();

    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        // 0: MSAA color (samples=N, CLEAR, storeOp=DONT_CARE)
        // 1: MSAA depth (samples=N, CLEAR, storeOp=DONT_CARE)
        // 2: Resolve color (samples=1, storeOp=STORE, finalLayout=TRANSFER_SRC)
        std::array<VkAttachmentDescription, 3> attachments{};

        // MSAA color
        attachments[0].format = colorFormat;
        attachments[0].samples = m_msaaSamples;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // MSAA depth
        attachments[1].format = depthFormat;
        attachments[1].samples = m_msaaSamples;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Resolve color (1x sample)
        attachments[2].format = colorFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        subpass.pResolveAttachments = &resolveRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_sceneRenderPass) != VK_SUCCESS)
        {
            return make_error("Failed to create scene render pass (MSAA)", ErrorCode::VulkanRenderPassCreationFailed);
        }
    }
    else
    {
        // Non-MSAA render pass
        // 0: Color (samples=1, CLEAR, STORE, finalLayout=SHADER_READ_ONLY)
        // 1: Depth (samples=1, CLEAR)
        std::array<VkAttachmentDescription, 2> attachments{};

        // Color
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Depth
        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_sceneRenderPass) != VK_SUCCESS)
        {
            return make_error("Failed to create scene render pass", ErrorCode::VulkanRenderPassCreationFailed);
        }
    }

    return {};
}

Result<> Vulkan::create_scene_render_target()
{
    VkDevice device = m_vulkanDevice.get_device();
    VkFormat colorFormat = m_swapchain.get_format();
    m_sceneColorMemoryBytes = 0;
    m_sceneDepthMemoryBytes = 0;
    m_msaaColorMemoryBytes = 0;

    auto depthFormatResult = find_depth_format();
    if (!depthFormatResult)
        return make_error(depthFormatResult.error());
    VkFormat depthFormat = depthFormatResult.value();

    // Resolve/final color image (1x sample), sampled by ImGui viewport.
    VkDeviceSize sceneColorAllocBytes = 0;
    if (auto res = m_vulkanDevice.create_image(
        m_sceneRenderWidth,
        m_sceneRenderHeight,
        colorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_sceneColorImage,
        m_sceneColorMemory,
        VK_SAMPLE_COUNT_1_BIT,
        &sceneColorAllocBytes
    ); !res)
        return res;
    m_sceneColorMemoryBytes = static_cast<std::uint64_t>(sceneColorAllocBytes);

    auto colorViewResult = m_vulkanDevice.create_image_view(m_sceneColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!colorViewResult)
        return make_error(colorViewResult.error());
    m_sceneColorView = colorViewResult.value();

    // Create depth image (at MSAA sample count)
    VkDeviceSize sceneDepthAllocBytes = 0;
    if (auto res = m_vulkanDevice.create_image(
        m_sceneRenderWidth,
        m_sceneRenderHeight,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_sceneDepthImage,
        m_sceneDepthMemory,
        m_msaaSamples,
        &sceneDepthAllocBytes
    ); !res)
        return res;
    m_sceneDepthMemoryBytes = static_cast<std::uint64_t>(sceneDepthAllocBytes);

    auto depthViewResult = m_vulkanDevice.create_image_view(m_sceneDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depthViewResult)
        return make_error(depthViewResult.error());
    m_sceneDepthView = depthViewResult.value();

    // If MSAA, create MSAA color image
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        VkDeviceSize msaaColorAllocBytes{};
        if (auto res = m_vulkanDevice.create_image(
            m_sceneRenderWidth,
            m_sceneRenderHeight,
            colorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            m_msaaColorImage,
            m_msaaColorMemory,
            m_msaaSamples,
            &msaaColorAllocBytes
        ); !res)
            return res;
        m_msaaColorMemoryBytes = static_cast<std::uint64_t>(msaaColorAllocBytes);

        auto msaaViewResult = m_vulkanDevice.create_image_view(m_msaaColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!msaaViewResult)
            return make_error(msaaViewResult.error());
        m_msaaColorView = msaaViewResult.value();
    }

    std::vector<VkImageView> fbAttachments{};
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        // Attachments: 0=MSAA color, 1=MSAA depth, 2=resolve color
        fbAttachments = {m_msaaColorView, m_sceneDepthView, m_sceneColorView};
    }
    else
    {
        // Attachments: 0=color, 1=depth
        fbAttachments = {m_sceneColorView, m_sceneDepthView};
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_sceneRenderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
    fbInfo.pAttachments = fbAttachments.data();
    fbInfo.width = m_sceneRenderWidth;
    fbInfo.height = m_sceneRenderHeight;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_sceneFramebuffer) != VK_SUCCESS)
    {
        return make_error("Failed to create scene framebuffer", ErrorCode::VulkanFramebufferCreationFailed);
    }

    return {};
}

void Vulkan::cleanup_scene_render_target()
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
    {
        m_sceneColorMemoryBytes = 0;
        m_sceneDepthMemoryBytes = 0;
        m_msaaColorMemoryBytes = 0;
        return;
    }

    if (m_sceneFramebuffer != nullptr)
    {
        vkDestroyFramebuffer(device, m_sceneFramebuffer, nullptr);
        m_sceneFramebuffer = nullptr;
    }

    // MSAA color
    if (m_msaaColorView != nullptr)
    {
        vkDestroyImageView(device, m_msaaColorView, nullptr);
        m_msaaColorView = nullptr;
    }
    if (m_msaaColorImage != nullptr)
    {
        vkDestroyImage(device, m_msaaColorImage, nullptr);
        m_msaaColorImage = nullptr;
    }
    if (m_msaaColorMemory != nullptr)
    {
        vkFreeMemory(device, m_msaaColorMemory, nullptr);
        m_msaaColorMemory = nullptr;
    }
    m_msaaColorMemoryBytes = 0;

    // Depth
    if (m_sceneDepthView != nullptr)
    {
        vkDestroyImageView(device, m_sceneDepthView, nullptr);
        m_sceneDepthView = nullptr;
    }
    if (m_sceneDepthImage != nullptr)
    {
        vkDestroyImage(device, m_sceneDepthImage, nullptr);
        m_sceneDepthImage = nullptr;
    }
    if (m_sceneDepthMemory != nullptr)
    {
        vkFreeMemory(device, m_sceneDepthMemory, nullptr);
        m_sceneDepthMemory = nullptr;
    }
    m_sceneDepthMemoryBytes = 0;

    // Scene color
    if (m_sceneColorView != nullptr)
    {
        vkDestroyImageView(device, m_sceneColorView, nullptr);
        m_sceneColorView = nullptr;
    }
    if (m_sceneColorImage != nullptr)
    {
        vkDestroyImage(device, m_sceneColorImage, nullptr);
        m_sceneColorImage = nullptr;
    }
    if (m_sceneColorMemory != nullptr)
    {
        vkFreeMemory(device, m_sceneColorMemory, nullptr);
        m_sceneColorMemory = nullptr;
    }
    m_sceneColorMemoryBytes = 0;
}

void Vulkan::cleanup_scene_render_pass()
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
        return;

    if (m_sceneRenderPass != nullptr)
    {
        vkDestroyRenderPass(device, m_sceneRenderPass, nullptr);
        m_sceneRenderPass = nullptr;
    }
}

Result<> Vulkan::create_nis_resources()
{
    VkDevice device = m_vulkanDevice.get_device();
    VkFormat colorFormat = m_swapchain.get_format();

    // Compile NIS compute shader (if not already compiled)
    if (m_nisComputeSpirv.empty())
    {
        std::filesystem::path nisDir = std::filesystem::path("Resources/NIS");
        auto compResult = ShaderCompiler::compile_compute_with_includes(
            m_nisShaderPath,
            {nisDir}
        );
        if (!compResult)
            return make_error(compResult.error());
        m_nisComputeSpirv = std::move(compResult.value());
    }

    // Output image (swapchain size, format as scene color, with STORAGE)
    VkExtent2D swapExtent = m_swapchain.get_extent();
    if (auto res = m_vulkanDevice.create_image(
        swapExtent.width,
        swapExtent.height,
        colorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_nisOutputImage,
        m_nisOutputMemory
    ); !res)
        return res;

    auto outViewResult = m_vulkanDevice.create_image_view(m_nisOutputImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!outViewResult)
        return make_error(outViewResult.error());
    m_nisOutputView = outViewResult.value();

    // Transition output image to GENERAL layout once
    if (auto res = m_vulkanDevice.transition_image_layout(
            m_nisOutputImage,
        colorFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL
    ); !res)
        return res;

    // Coefficient textures (kFilterSize/4 x kPhaseCount, RGBA32F)
    // coef_scale and coef_usm are float[kPhaseCount][kFilterSize] = float[64][8]
    // Stored as RGBA32F textures of width kFilterSize/4=2, height kPhaseCount=64.
    const std::uint32_t coefWidth{kFilterSize / 4};                          // 2
    const std::uint32_t coefHeight {kPhaseCount};                            // 64
    VkDeviceSize coefImageSize = coefWidth * coefHeight * 4 * sizeof(float); // RGBA32F

    auto uploadCoefTexture = [&](
        const float coeffData[][kFilterSize],
        VkImage& image,
        VkDeviceMemory& memory,
        VkImageView& view
        ) -> Result<>
    {
        if (auto res = m_vulkanDevice.create_image(
            coefWidth,
            coefHeight,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            image,
            memory
        ); !res)
            return res;

        VkBuffer stagingBuffer{};
        VkDeviceMemory stagingMemory{};
        if (auto res = m_vulkanDevice.create_buffer(
            coefImageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingMemory
        ); !res)
            return res;

        void* mapped{};
        vkMapMemory(device, stagingMemory, 0, coefImageSize, 0, &mapped);
        std::memcpy(mapped, coeffData, static_cast<std::size_t>(coefImageSize));
        vkUnmapMemory(device, stagingMemory);

        // Transition, copy, transition
        if (auto res = m_vulkanDevice.transition_image_layout(
            image,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        ); !res)
        {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return res;
        }

        if (auto res = m_vulkanDevice.copy_buffer_to_image(
            stagingBuffer,
            image,
            coefWidth,
            coefHeight
        ); !res)
        {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return res;
        }

        if (auto res = m_vulkanDevice.transition_image_layout(
            image,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ); !res)
        {
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return res;
        }

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);

        auto viewResult = m_vulkanDevice.create_image_view(
            image,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
        if (!viewResult)
            return make_error(viewResult.error());
        view = viewResult.value();

        return {};
    };

    if (auto res = uploadCoefTexture(coef_scale, m_nisCoefScalerImage, m_nisCoefScalerMemory, m_nisCoefScalerView); !res)
        return res;
    if (auto res = uploadCoefTexture(coef_usm, m_nisCoefUsmImage, m_nisCoefUsmMemory, m_nisCoefUsmView); !res)
        return res;

    // Linear clamp sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_nisLinearSampler) != VK_SUCCESS)
        return make_error("Failed to create NIS linear sampler", ErrorCode::VulkanGraphicsPipelineCreationFailed);

    // Config UBO (host-visible, persistently mapped)
    if (auto res = m_vulkanDevice.create_buffer(
        sizeof(NISConfig),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_nisConfigBuffer,
        m_nisConfigMemory
    ); !res)
        return res;

    update_nis_config();

    // Descriptor set layout
    // Bindings match NIS_Main.glsl:
    // 0: UBO (NISConfig)    1: sampler    2: input texture
    // 3: output image       4: coef_scaler   5: coef_usm
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[4] = {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[5] = {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_nisDescriptorSetLayout) != VK_SUCCESS)
        return make_error("Failed to create NIS descriptor set layout", ErrorCode::VulkanGraphicsPipelineCreationFailed);

    // Descriptor pool 
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_SAMPLER, 1};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3};
    poolSizes[3] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_nisDescriptorPool) != VK_SUCCESS)
        return make_error("Failed to create NIS descriptor pool", ErrorCode::VulkanGraphicsPipelineCreationFailed);

    // Allocate descriptor set 
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_nisDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_nisDescriptorSetLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &m_nisDescriptorSet) != VK_SUCCESS)
        return make_error("Failed to allocate NIS descriptor set", ErrorCode::VulkanGraphicsPipelineCreationFailed);

    // Write descriptor set
    VkDescriptorBufferInfo configBufInfo{};
    configBufInfo.buffer = m_nisConfigBuffer;
    configBufInfo.offset = 0;
    configBufInfo.range = sizeof(NISConfig);

    VkDescriptorImageInfo samplerImgInfo{};
    samplerImgInfo.sampler = m_nisLinearSampler;

    VkDescriptorImageInfo inputImgInfo{};
    inputImgInfo.imageView = m_sceneColorView;
    inputImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo outputImgInfo{};
    outputImgInfo.imageView = m_nisOutputView;
    outputImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo coefScalerInfo{};
    coefScalerInfo.imageView = m_nisCoefScalerView;
    coefScalerInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo coefUsmInfo{};
    coefUsmInfo.imageView = m_nisCoefUsmView;
    coefUsmInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 6> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_nisDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &configBufInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_nisDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &samplerImgInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_nisDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[2].pImageInfo = &inputImgInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_nisDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].pImageInfo = &outputImgInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = m_nisDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[4].pImageInfo = &coefScalerInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = m_nisDescriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorCount = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].pImageInfo = &coefUsmInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    //Compute pipeline
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = m_nisComputeSpirv.size() * sizeof(uint32_t);
    moduleInfo.pCode = m_nisComputeSpirv.data();

    VkShaderModule computeModule{};
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &computeModule) != VK_SUCCESS)
        return make_error("Failed to create NIS compute shader module", ErrorCode::VulkanGraphicsPipelineCreationFailed);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeModule;
    stageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &m_nisDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_nisPipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, computeModule, nullptr);
        return make_error("Failed to create NIS pipeline layout", ErrorCode::VulkanGraphicsPipelineCreationFailed);
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_nisPipelineLayout;

    if (vkCreateComputePipelines(device, nullptr, 1, &pipelineInfo, nullptr, &m_nisComputePipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, computeModule, nullptr);
        return make_error("Failed to create NIS compute pipeline", ErrorCode::VulkanGraphicsPipelineCreationFailed);
    }

    vkDestroyShaderModule(device, computeModule, nullptr);

    fmt::print(
        "NIS: compute pipeline created ({}x{}, sharpness={:.2f})\n",
        m_sceneRenderWidth, m_sceneRenderHeight, m_nisSharpness
    );

    return {};
}

void Vulkan::cleanup_nis_resources()
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device) return;

    if (m_nisComputePipeline != nullptr)
    { vkDestroyPipeline(device, m_nisComputePipeline, nullptr); m_nisComputePipeline = nullptr; }
    if (m_nisPipelineLayout != nullptr)
    { vkDestroyPipelineLayout(device, m_nisPipelineLayout, nullptr); m_nisPipelineLayout = nullptr; }
    if (m_nisDescriptorPool != nullptr)
    { vkDestroyDescriptorPool(device, m_nisDescriptorPool, nullptr); m_nisDescriptorPool = nullptr; }
    m_nisDescriptorSet = nullptr; // freed with pool
    if (m_nisDescriptorSetLayout != nullptr)
    { vkDestroyDescriptorSetLayout(device, m_nisDescriptorSetLayout, nullptr); m_nisDescriptorSetLayout = nullptr; }

    if (m_nisOutputView != nullptr)
    { vkDestroyImageView(device, m_nisOutputView, nullptr); m_nisOutputView = nullptr; }
    if (m_nisOutputImage != nullptr)
    { vkDestroyImage(device, m_nisOutputImage, nullptr); m_nisOutputImage = nullptr; }
    if (m_nisOutputMemory != nullptr)
    { vkFreeMemory(device, m_nisOutputMemory, nullptr); m_nisOutputMemory = nullptr; }

    if (m_nisCoefScalerView != nullptr)
    { vkDestroyImageView(device, m_nisCoefScalerView, nullptr); m_nisCoefScalerView = nullptr; }
    if (m_nisCoefScalerImage != nullptr)
    { vkDestroyImage(device, m_nisCoefScalerImage, nullptr); m_nisCoefScalerImage = nullptr; }
    if (m_nisCoefScalerMemory != nullptr)
    { vkFreeMemory(device, m_nisCoefScalerMemory, nullptr); m_nisCoefScalerMemory = nullptr; }

    if (m_nisCoefUsmView != nullptr)
    { vkDestroyImageView(device, m_nisCoefUsmView, nullptr); m_nisCoefUsmView = nullptr; }
    if (m_nisCoefUsmImage != nullptr)
    { vkDestroyImage(device, m_nisCoefUsmImage, nullptr); m_nisCoefUsmImage = nullptr; }
    if (m_nisCoefUsmMemory != nullptr)
    { vkFreeMemory(device, m_nisCoefUsmMemory, nullptr); m_nisCoefUsmMemory = nullptr; }

    if (m_nisConfigBuffer != nullptr)
    { vkDestroyBuffer(device, m_nisConfigBuffer, nullptr); m_nisConfigBuffer = nullptr; }
    if (m_nisConfigMemory != nullptr)
    { vkFreeMemory(device, m_nisConfigMemory, nullptr); m_nisConfigMemory = nullptr; }

    if (m_nisLinearSampler != nullptr)
    { vkDestroySampler(device, m_nisLinearSampler, nullptr); m_nisLinearSampler = nullptr; }
}

void Vulkan::update_nis_config()
{
    NISConfig config{};
    VkExtent2D swapExtent = m_swapchain.get_extent();
    
    // NIS upscaling mode: input is render resolution, output is swapchain (viewport) resolution
    const bool configValid = NVScalerUpdateConfig(
        config,
        m_nisSharpness,
        0,
        0,
        m_sceneRenderWidth,
        m_sceneRenderHeight,
        m_sceneRenderWidth,
        m_sceneRenderHeight,
        0,
        0,
        swapExtent.width,
        swapExtent.height,
        swapExtent.width,
        swapExtent.height,
        NISHDRMode::None
    );

    if (!configValid)
    {
        fmt::print(
            "Warning: NIS config is invalid for render scale {:.2f}. "
            "NIS upscaling supports only 0.50..1.00 scale factors.\n",
            m_renderScale
        );
        return;
    }

    // Upload to UBO
    VkDevice device = m_vulkanDevice.get_device();
    void* mapped{};
    if (vkMapMemory(device, m_nisConfigMemory, 0, sizeof(NISConfig), 0, &mapped) == VK_SUCCESS)
    {
        std::memcpy(mapped, &config, sizeof(NISConfig));
        vkUnmapMemory(device, m_nisConfigMemory);
    }
}

void Vulkan::dispatch_nis_pass(VkCommandBuffer cmd)
{
    if (m_nisComputePipeline == nullptr || m_nisDescriptorSet == nullptr)
        return;

    // Transition scene color from COLOR_ATTACHMENT_OUTPUT to SHADER_READ for compute input
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneColorImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );
    }

    // Transition NIS output to GENERAL for compute write (discarding previous frame's contents)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_nisOutputImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, 
            nullptr,
            0, 
            nullptr,
            1, 
            &barrier
        );
    }

    // Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_nisComputePipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_nisPipelineLayout,
        0, 
        1, 
        &m_nisDescriptorSet, 
        0, 
        nullptr
    );

    // Dispatch using NIS optimal block sizes based on output (viewport) resolution
    NISOptimizer opt(true, NISGPUArchitecture::NVIDIA_Generic);
    std::uint32_t blockWidth = opt.GetOptimalBlockWidth();
    std::uint32_t blockHeight = opt.GetOptimalBlockHeight();
    VkExtent2D swapExtent = m_swapchain.get_extent();
    std::uint32_t groupsX = (swapExtent.width + blockWidth - 1) / blockWidth;
    std::uint32_t groupsY = (swapExtent.height + blockHeight - 1) / blockHeight;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition NIS output from GENERAL to SHADER_READ_ONLY for ImGui sampling
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_nisOutputImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr, 
            0, 
            nullptr,
            1, 
            &barrier
        );
    }
}

VkImageView Vulkan::get_active_scene_view() const noexcept
{
    if (m_nisEnabled && m_nisOutputView != nullptr)
        return m_nisOutputView;
    return m_sceneColorView;
}

/// Runtime settings

void Vulkan::set_vsync(KHR_Settings mode) noexcept
{
    VkPresentModeKHR requestedMode = VK_PRESENT_MODE_FIFO_KHR;
    switch (mode)
    {
    case KHR_Settings::VSync:
        requestedMode = VK_PRESENT_MODE_FIFO_KHR;
        break;
    case KHR_Settings::Triple_Buffering:
        requestedMode = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
    case KHR_Settings::Immediate:
        requestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        break;
    default:
        requestedMode = VK_PRESENT_MODE_FIFO_KHR;
        break;
    }

    const VkPresentModeKHR currentMode = m_swapchain.get_present_mode();
    if (currentMode == requestedMode)
    {
        m_vsyncEnabled = (currentMode == VK_PRESENT_MODE_FIFO_KHR || currentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        return;
    }

    const bool previousVsync = m_vsyncEnabled;
    m_swapchain.set_desired_present_mode(requestedMode);

    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    if (auto result = recreate_swap_chain(); !result)
    {
        fmt::print("Warning: failed to apply present mode change: {}\n", result.error().message);
        m_vsyncEnabled = previousVsync;
        // Fall back to deferred recreate retry on next frame.
        m_framebufferResized = true;
        return;
    }

    const VkPresentModeKHR appliedMode = m_swapchain.get_present_mode();
    m_vsyncEnabled = (appliedMode == VK_PRESENT_MODE_FIFO_KHR || appliedMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
}

bool Vulkan::get_vsync() const noexcept
{
    const VkPresentModeKHR mode = m_swapchain.get_present_mode();
    return (mode == VK_PRESENT_MODE_FIFO_KHR || mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR);
}

void Vulkan::set_msaa_samples(std::int32_t samples) noexcept
{
    VkSampleCountFlagBits desired = VK_SAMPLE_COUNT_1_BIT;
    switch (samples)
    {
    case 2:
        desired = VK_SAMPLE_COUNT_2_BIT;
        break;
    case 4:
        desired = VK_SAMPLE_COUNT_4_BIT;
        break;
    case 8:
        desired = VK_SAMPLE_COUNT_8_BIT;
        break;
    default:
        desired = VK_SAMPLE_COUNT_1_BIT;
        break;
    }

    VkSampleCountFlagBits maxSamples = get_max_usable_sample_count();
    if (desired > maxSamples)
        desired = maxSamples;

    if (desired == m_msaaSamples)
        return;

    const VkSampleCountFlagBits previous = m_msaaSamples;
    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    auto rebuild_for_msaa = [&](VkSampleCountFlagBits targetSamples) -> Result<> {
        m_msaaSamples = targetSamples;
        if (m_nisEnabled)
            cleanup_nis_resources();
        cleanup_scene_render_target();
        cleanup_scene_render_pass();
        m_pipeline.cleanup();

        if (auto result = create_scene_render_pass(); !result)
            return result;
        compute_scene_render_size();
        if (auto result = create_scene_render_target(); !result)
            return result;
        
        if (m_nisEnabled)
        {
            if (auto nisResult = create_nis_resources(); !nisResult)
                fmt::print("Warning: failed to recreate NIS resources after MSAA change: {}\n", nisResult.error().message);
        }

        MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
        return m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv);
    };

    if (auto result = rebuild_for_msaa(desired); !result)
    {
        fmt::print("Warning: failed to apply MSAA setting: {}\n", result.error().message);
        if (auto rollback = rebuild_for_msaa(previous); !rollback)
        {
            fmt::print("Warning: failed to restore previous MSAA state: {}\n", rollback.error().message);
            if (auto recreate = recreate_swap_chain(); !recreate)
            {
                fmt::print("Warning: failed to recover after MSAA setting failure: {}\n", recreate.error().message);
                m_framebufferResized = true;
            }
        }
    }
    else
    {
        if (m_swapchainRecreatedCallback)
            m_swapchainRecreatedCallback();
    }
}

std::int32_t Vulkan::get_msaa_samples() const noexcept
{
    return static_cast<std::int32_t>(m_msaaSamples);
}

void Vulkan::set_alpha_to_coverage(bool enabled) noexcept
{
    if (enabled == m_alphaToCoverageEnabled)
        return;
    m_alphaToCoverageEnabled = enabled;

    // Pipeline-only rebuild (no render pass / framebuffer change)
    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);
    m_pipeline.cleanup();
    MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
    if (auto result = m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv); !result)
    {
        fmt::print("Warning: failed to apply alpha-to-coverage setting: {}\n", result.error().message);
        m_alphaToCoverageEnabled = !enabled;
        m_pipeline.cleanup();
        MultisampleConfig rollback{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
        (void)m_pipeline.initialize(m_sceneRenderPass, rollback, m_vertSpirv, m_fragSpirv);
    }
}

bool Vulkan::get_alpha_to_coverage() const noexcept
{
    return m_alphaToCoverageEnabled;
}

void Vulkan::set_sample_shading(bool enabled) noexcept
{
    if (enabled == m_sampleShadingEnabled)
        return;
    m_sampleShadingEnabled = enabled;

    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);
    m_pipeline.cleanup();
    MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
    if (auto result = m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv); !result)
    {
        fmt::print("Warning: failed to apply sample shading setting: {}\n", result.error().message);
        m_sampleShadingEnabled = !enabled;
        m_pipeline.cleanup();
        MultisampleConfig rollback{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
        (void)m_pipeline.initialize(m_sceneRenderPass, rollback, m_vertSpirv, m_fragSpirv);
    }
}

bool Vulkan::get_sample_shading() const noexcept
{
    return m_sampleShadingEnabled;
}

void Vulkan::set_min_sample_shading(float fraction) noexcept
{
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (std::fabs(fraction - m_minSampleShading) < 0.0001f)
        return;
    m_minSampleShading = fraction;

    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);
    m_pipeline.cleanup();
    MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
    if (auto result = m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv); !result)
    {
        fmt::print("Warning: failed to apply min sample shading setting: {}\n", result.error().message);
        m_pipeline.cleanup();
        MultisampleConfig rollback{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
        (void)m_pipeline.initialize(m_sceneRenderPass, rollback, m_vertSpirv, m_fragSpirv);
    }
}

float Vulkan::get_min_sample_shading() const noexcept
{
    return m_minSampleShading;
}

void Vulkan::set_nis_enabled(bool enabled) noexcept
{
    if (enabled == m_nisEnabled)
        return;
    m_nisEnabled = enabled;

    constexpr float kMinNisRenderScale = 0.5f;

    if (m_nisEnabled && m_renderScale < kMinNisRenderScale)
    {
        m_renderScale = kMinNisRenderScale;
        m_framebufferResized = true;
        fmt::print("NIS enabled: render scale clamped to {:.2f} (NIS supports 0.50..1.00 upscaling range).\n", kMinNisRenderScale);
        return;
    }

    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    if (m_nisEnabled)
    {
        if (auto result = create_nis_resources(); !result)
        {
            fmt::print("Warning: failed to create NIS resources: {}\n", result.error().message);
            m_nisEnabled = false;
        }
    }
    else
    {
        cleanup_nis_resources();
    }
}

bool Vulkan::get_nis_enabled() const noexcept
{
    return m_nisEnabled;
}

void Vulkan::set_nis_sharpness(float sharpness) noexcept
{
    sharpness = std::clamp(sharpness, 0.0f, 1.0f);
    if (std::fabs(sharpness - m_nisSharpness) < 0.001f)
        return;
    m_nisSharpness = sharpness;

    if (m_nisEnabled && m_nisConfigBuffer != nullptr)
    {
        update_nis_config();
    }
}

float Vulkan::get_nis_sharpness() const noexcept
{
    return m_nisSharpness;
}

void Vulkan::set_render_scale(float scale) noexcept
{
    constexpr float kMinRenderScale = 0.25f;
    constexpr float kMinNisRenderScale = 0.5f;
    scale = std::clamp(scale, kMinRenderScale, 2.0f);

    if (m_nisEnabled && scale < kMinNisRenderScale)
    {
        scale = kMinNisRenderScale;
        fmt::print("NIS requires render scale >= {:.2f}; clamping requested value.\n", kMinNisRenderScale);
    }

    if (std::fabs(scale - m_renderScale) < 0.0001f)
        return;

    m_renderScale = scale;
    m_framebufferResized = true;
}

float Vulkan::get_render_scale() const noexcept
{
    return m_renderScale;
}

/// Shader management

void Vulkan::set_shader_paths(const std::filesystem::path& vertPath, const std::filesystem::path& fragPath)
{
    m_vertShaderPath = vertPath;
    m_fragShaderPath = fragPath;
}

Result<> Vulkan::recompile_shaders()
{
    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    auto vertResult = ShaderCompiler::compile_file(m_vertShaderPath);
    if (!vertResult)
        return make_error(vertResult.error());

    auto fragResult = ShaderCompiler::compile_file(m_fragShaderPath);
    if (!fragResult)
        return make_error(fragResult.error());

    m_vertSpirv = std::move(vertResult.value());
    m_fragSpirv = std::move(fragResult.value());

    m_pipeline.cleanup();
    MultisampleConfig msConfig{m_msaaSamples, m_alphaToCoverageEnabled, m_sampleShadingEnabled, m_minSampleShading};
    return m_pipeline.initialize(m_sceneRenderPass, msConfig, m_vertSpirv, m_fragSpirv);
}
