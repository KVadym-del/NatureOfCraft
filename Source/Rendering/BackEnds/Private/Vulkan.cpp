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

    if (auto result = m_pipeline.initialize(m_sceneRenderPass, m_msaaSamples, m_vertSpirv, m_fragSpirv); !result)
        return result;

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

    uint32_t totalTriangles = 0;
    for (const auto& renderable : m_renderables)
    {
        if (renderable.meshIndex < m_meshes.size())
            totalTriangles += m_meshes[renderable.meshIndex].indexCount / 3;
    }
    m_totalTriangleCountCached = totalTriangles;
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

    if (m_sceneFramebuffer == VK_NULL_HANDLE || m_sceneRenderPass == VK_NULL_HANDLE || m_sceneColorImage == VK_NULL_HANDLE)
    {
        if (auto recreateResult = recreate_swap_chain(); !recreateResult)
            return recreateResult;
        return {};
    }

    uint32_t imageIndex{};
    VkResult result = vkAcquireNextImageKHR(device, m_swapchain.get_swapchain(), UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

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

    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    const VkResult submitResult =
        vkQueueSubmit(m_vulkanDevice.get_graphics_queue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]);
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

    VkSwapchainKHR swapChains[] = {m_swapchain.get_swapchain()};
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
        if (mesh.indexBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
        if (mesh.indexBufferMemory != VK_NULL_HANDLE)
            vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
        if (mesh.vertexBufferMemory != VK_NULL_HANDLE)
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
        if (tex.imageView != VK_NULL_HANDLE)
            vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image != VK_NULL_HANDLE)
            vkDestroyImage(device, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE)
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

    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
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
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_instanceBufferMappings[i] != nullptr && m_instanceBufferMemories[i] != VK_NULL_HANDLE)
        {
            vkUnmapMemory(device, m_instanceBufferMemories[i]);
            m_instanceBufferMappings[i] = nullptr;
        }
        if (m_instanceBuffers[i] != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, m_instanceBuffers[i], nullptr);
            m_instanceBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_instanceBufferMemories[i] != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_instanceBufferMemories[i], nullptr);
            m_instanceBufferMemories[i] = VK_NULL_HANDLE;
        }
        m_instanceBufferAllocatedBytes[i] = 0;
    }
    m_instanceBufferCapacity = 0;
    m_instanceBufferMemoryBytes = 0;

    cleanup_upload_staging_buffer();

    // Destroy sync objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_imageAvailableSemaphores.size() > i && m_imageAvailableSemaphores[i])
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
        if (m_inFlightFences.size() > i && m_inFlightFences[i])
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_inFlightFences.clear();

    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++)
    {
        if (m_renderFinishedSemaphores[i])
            vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_renderFinishedSemaphores.clear();
    m_imagesInFlight.clear();

    if (!m_commandBuffers.empty() && m_vulkanDevice.get_command_pool() != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device, m_vulkanDevice.get_command_pool(), static_cast<uint32_t>(m_commandBuffers.size()),
                             m_commandBuffers.data());
        m_commandBuffers.clear();
    }

    // Destroy offscreen resources
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
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
    {
        return make_error("Failed to allocate command buffer", ErrorCode::VulkanCommandBufferAllocationFailed);
    }

    return {};
}

Result<> Vulkan::ensure_upload_staging_capacity(VkDeviceSize requiredSize)
{
    if (requiredSize <= m_uploadStagingCapacity && m_uploadStagingBuffer != VK_NULL_HANDLE && m_uploadStagingMapped)
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

    VkDeviceSize allocatedBytes = 0;
    auto createResult = m_vulkanDevice.create_buffer(
        newCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uploadStagingBuffer,
        m_uploadStagingBufferMemory, &allocatedBytes);
    if (!createResult)
        return make_error(createResult.error());

    if (vkMapMemory(device, m_uploadStagingBufferMemory, 0, newCapacity, 0, &m_uploadStagingMapped) != VK_SUCCESS)
    {
        cleanup_upload_staging_buffer();
        return make_error("Failed to map upload staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }

    m_uploadStagingCapacity = newCapacity;
    m_uploadStagingMemoryBytes = static_cast<uint64_t>(allocatedBytes);
    return {};
}

void Vulkan::cleanup_upload_staging_buffer() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
    {
        m_uploadStagingBuffer = VK_NULL_HANDLE;
        m_uploadStagingBufferMemory = VK_NULL_HANDLE;
        m_uploadStagingMapped = nullptr;
        m_uploadStagingCapacity = 0;
        m_uploadStagingMemoryBytes = 0;
        return;
    }

    if (m_uploadStagingMapped != nullptr && m_uploadStagingBufferMemory != VK_NULL_HANDLE)
    {
        vkUnmapMemory(device, m_uploadStagingBufferMemory);
        m_uploadStagingMapped = nullptr;
    }
    if (m_uploadStagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, m_uploadStagingBuffer, nullptr);
        m_uploadStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_uploadStagingBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_uploadStagingBufferMemory, nullptr);
        m_uploadStagingBufferMemory = VK_NULL_HANDLE;
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
            bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, newBuffers[i], newMemories[i],
            &newAllocatedBytes[i]);
        if (!result)
        {
            VkDevice device = m_vulkanDevice.get_device();
            for (size_t j = 0; j <= i; ++j)
            {
                if (newMappings[j] != nullptr && newMemories[j] != VK_NULL_HANDLE)
                    vkUnmapMemory(device, newMemories[j]);
                if (newBuffers[j] != VK_NULL_HANDLE)
                    vkDestroyBuffer(device, newBuffers[j], nullptr);
                if (newMemories[j] != VK_NULL_HANDLE)
                    vkFreeMemory(device, newMemories[j], nullptr);
            }
            return make_error(result.error());
        }

        if (vkMapMemory(m_vulkanDevice.get_device(), newMemories[i], 0, bufferSize, 0, &newMappings[i]) != VK_SUCCESS)
        {
            VkDevice device = m_vulkanDevice.get_device();
            for (size_t j = 0; j <= i; ++j)
            {
                if (newMappings[j] != nullptr && newMemories[j] != VK_NULL_HANDLE)
                    vkUnmapMemory(device, newMemories[j]);
                if (newBuffers[j] != VK_NULL_HANDLE)
                    vkDestroyBuffer(device, newBuffers[j], nullptr);
                if (newMemories[j] != VK_NULL_HANDLE)
                    vkFreeMemory(device, newMemories[j], nullptr);
            }
            return make_error("Failed to map instance buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
        }
    }

    VkDevice device = m_vulkanDevice.get_device();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_instanceBufferMappings[i] != nullptr && m_instanceBufferMemories[i] != VK_NULL_HANDLE)
            vkUnmapMemory(device, m_instanceBufferMemories[i]);
        if (m_instanceBuffers[i] != VK_NULL_HANDLE)
            vkDestroyBuffer(device, m_instanceBuffers[i], nullptr);
        if (m_instanceBufferMemories[i] != VK_NULL_HANDLE)
            vkFreeMemory(device, m_instanceBufferMemories[i], nullptr);
        m_instanceBuffers[i] = newBuffers[i];
        m_instanceBufferMemories[i] = newMemories[i];
        m_instanceBufferMappings[i] = newMappings[i];
        m_instanceBufferAllocatedBytes[i] = newAllocatedBytes[i];
    }

    m_instanceBufferCapacity = newCapacity;
    m_instanceBufferMemoryBytes = 0;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        m_instanceBufferMemoryBytes += static_cast<uint64_t>(m_instanceBufferAllocatedBytes[i]);
    return {};
}

Result<> Vulkan::record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept
{
    VkDevice device = m_vulkanDevice.get_device();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        return make_error("Failed to begin recording command buffer", ErrorCode::VulkanCommandBufferRecordingFailed);
    }

    // ==========================================
    // Pass 1: Scene render pass (offscreen)
    // ==========================================
    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_sceneRenderPass;
        renderPassInfo.framebuffer = m_sceneFramebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {m_sceneRenderWidth, m_sceneRenderHeight};

        // Clear values: color + depth (+ MSAA color if applicable).
        std::array<VkClearValue, 3> clearValues{};
        uint32_t clearValueCount = 2;
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

        uint32_t culledRenderables = 0;
        for (const auto& renderable : m_renderables)
        {
            if (renderable.meshIndex >= m_meshes.size())
                continue;

            const auto& mesh = m_meshes[renderable.meshIndex];
            if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
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

            uint32_t materialIndex = renderable.materialIndex;
            if (materialIndex >= m_materials.size())
                materialIndex = m_defaultMaterialIndex;

            XMMATRIX mvp = XMMatrixMultiply(world, viewProj);
            InstanceData instanceData{};
            XMStoreFloat4x4(&instanceData.mvp, mvp);
            instanceData.model = renderable.worldMatrix;

            const uint32_t firstInstance = static_cast<uint32_t>(m_instanceDataScratch.size());
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
                InstanceBatch{renderable.meshIndex, materialIndex, firstInstance, 1});
        }

        m_lastVisibleRenderableCount = static_cast<uint32_t>(m_instanceDataScratch.size());
        m_lastCulledRenderableCount = culledRenderables;
        m_lastInstancedBatchCount = static_cast<uint32_t>(m_instanceBatchesScratch.size());
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
            if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
                continue;

            // Bind material descriptor set
            uint32_t matIdx = batch.materialIndex;
            if (matIdx < m_materials.size())
            {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                        &m_materials[matIdx].descriptorSet, 0, nullptr);
            }
            else if (!m_materials.empty())
            {
                // Fallback to default material (index 0)
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                        &m_materials[m_defaultMaterialIndex].descriptorSet, 0, nullptr);
            }

            if (m_instanceBuffers[m_currentFrame] == VK_NULL_HANDLE)
                continue;

            std::array<VkBuffer, 2> vertexBuffers{mesh.vertexBuffer, m_instanceBuffers[m_currentFrame]};
            std::array<VkDeviceSize, 2> offsets{0, 0};
            vkCmdBindVertexBuffers(commandBuffer, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(),
                                   offsets.data());

            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, batch.instanceCount, 0, 0, batch.firstInstance);
            ++m_lastDrawCallCount;
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    // Scene color image is sampled in the ImGui viewport pass.
    // Add an explicit visibility barrier from color output writes to fragment shader reads.
    {
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

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sceneReadBarrier);
    }

    // ==========================================
    // Pass 2: UI render pass (swapchain)
    // ==========================================
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
    uint32_t imageCount = m_swapchain.get_image_count();

    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    m_renderFinishedSemaphores.resize(imageCount);
    m_imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    auto rollback_sync_objects = [&]() {
        for (VkSemaphore semaphore : m_imageAvailableSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(device, semaphore, nullptr);
        }
        for (VkFence fence : m_inFlightFences)
        {
            if (fence != VK_NULL_HANDLE)
                vkDestroyFence(device, fence, nullptr);
        }
        for (VkSemaphore semaphore : m_renderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(device, semaphore, nullptr);
        }
        m_imageAvailableSemaphores.clear();
        m_inFlightFences.clear();
        m_renderFinishedSemaphores.clear();
        m_imagesInFlight.clear();
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS)
        {
            rollback_sync_objects();
            return make_error("Failed to create synchronization objects for a frame",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            rollback_sync_objects();
            return make_error("Failed to create synchronization objects for a frame",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    for (size_t i = 0; i < imageCount; i++)
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

    std::int32_t width = 0;
    std::int32_t height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    if (auto result = m_swapchain.recreate(); !result)
        return result;

    // Recreate offscreen scene render target (size may have changed)
    cleanup_scene_render_target();
    compute_scene_render_size();
    if (auto result = create_scene_render_target(); !result)
        return result;

    // Recreate per-image sync objects for the new swapchain.
    // Build the new set first, then atomically swap and destroy old ones.
    uint32_t imageCount = m_swapchain.get_image_count();
    std::vector<VkSemaphore> newRenderFinishedSemaphores(imageCount, VK_NULL_HANDLE);
    std::vector<VkFence> newImagesInFlight(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &newRenderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            for (VkSemaphore semaphore : newRenderFinishedSemaphores)
            {
                if (semaphore != VK_NULL_HANDLE)
                    vkDestroySemaphore(device, semaphore, nullptr);
            }
            return make_error("Failed to recreate render finished semaphore",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    for (VkSemaphore semaphore : m_renderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores = std::move(newRenderFinishedSemaphores);
    m_imagesInFlight = std::move(newImagesInFlight);

    // Notify listeners (e.g., ImGui) about the swapchain recreation
    if (m_swapchainRecreatedCallback)
    {
        m_swapchainRecreatedCallback();
    }

    return {};
}

/// GPU mesh upload

Result<uint32_t> Vulkan::upload_mesh(const MeshData& meshData)
{
    if (meshData.vertices.empty())
        return make_error("MeshData has no vertices", ErrorCode::AssetInvalidData);

    std::string meshKey;
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
    mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    auto cleanup_mesh_gpu_buffers = [&]() {
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
            mesh.indexBuffer = VK_NULL_HANDLE;
        }
        if (mesh.indexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
            mesh.indexBufferMemory = VK_NULL_HANDLE;
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
            mesh.vertexBuffer = VK_NULL_HANDLE;
        }
        if (mesh.vertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, mesh.vertexBufferMemory, nullptr);
            mesh.vertexBufferMemory = VK_NULL_HANDLE;
        }
    };

    // --- Vertex/index buffers ---
    const VkDeviceSize vertexBufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
    const VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();
    const VkDeviceSize indexSrcOffset = (vertexBufferSize + 3) & ~static_cast<VkDeviceSize>(3);
    const VkDeviceSize totalStagingSize = indexSrcOffset + indexBufferSize;
    mesh.vertexBufferBytes = vertexBufferSize;
    mesh.indexBufferBytes = indexBufferSize;
    VkDeviceSize vertexAllocationBytes = 0;
    VkDeviceSize indexAllocationBytes = 0;

    if (auto res = ensure_upload_staging_capacity(totalStagingSize); !res)
        return make_error(res.error());
    std::memcpy(m_uploadStagingMapped, meshData.vertices.data(), static_cast<size_t>(vertexBufferSize));
    std::memcpy(static_cast<std::byte*>(m_uploadStagingMapped) + indexSrcOffset, meshData.indices.data(),
                static_cast<size_t>(indexBufferSize));

    if (auto res = m_vulkanDevice.create_buffer(
            vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexBufferMemory, &vertexAllocationBytes);
        !res)
    {
        return make_error(res.error());
    }

    if (auto res = m_vulkanDevice.create_buffer(
            indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexBufferMemory, &indexAllocationBytes);
        !res)
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
    const float halfX = (meshData.boundsMax.x - meshData.boundsMin.x) * 0.5f;
    const float halfY = (meshData.boundsMax.y - meshData.boundsMin.y) * 0.5f;
    const float halfZ = (meshData.boundsMax.z - meshData.boundsMin.z) * 0.5f;
    mesh.boundsRadius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
    mesh.vertexBufferAllocationBytes = vertexAllocationBytes;
    mesh.indexBufferAllocationBytes = indexAllocationBytes;

    uint32_t meshIndex = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(mesh);
    m_meshMemoryBytes +=
        static_cast<uint64_t>(mesh.vertexBufferAllocationBytes + mesh.indexBufferAllocationBytes);
    if (!meshKey.empty())
        m_meshLookup.emplace(std::move(meshKey), meshIndex);
    return meshIndex;
}

/// GPU texture upload

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

Result<uint32_t> Vulkan::upload_texture(const TextureData& textureData)
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

    // Copy pixel data to staging buffer
    std::memcpy(m_uploadStagingMapped, textureData.pixels.data(), static_cast<size_t>(imageSize));

    // Create the VkImage
    GpuTexture texture{};
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize textureAllocationBytes = 0;

    if (auto res = m_vulkanDevice.create_image(textureData.width, textureData.height, format, VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.memory,
                                               VK_SAMPLE_COUNT_1_BIT, &textureAllocationBytes);
        !res)
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

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toTransferBarrier);

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
    vkCmdCopyBufferToImage(commandBuffer, m_uploadStagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copyRegion);

    VkImageMemoryBarrier toShaderReadBarrier = toTransferBarrier;
    toShaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toShaderReadBarrier);

    if (auto endResult = m_vulkanDevice.end_single_time_commands(commandBuffer); !endResult)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        return make_error("Failed to submit texture upload command buffer", ErrorCode::VulkanTextureUploadFailed);
    }

    // Create image view
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

    uint32_t textureIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(texture);
    m_textureMemoryBytes += static_cast<uint64_t>(texture.allocationBytes);
    if (!textureData.sourcePath.empty())
        m_textureLookup.emplace(textureData.sourcePath.generic_string(), textureIndex);
    return textureIndex;
}

/// Descriptor pool and materials

Result<> Vulkan::create_descriptor_pool()
{
    VkDevice device = m_vulkanDevice.get_device();

    // Pool sized for up to 256 materials, each needing 5 combined image samplers
    constexpr uint32_t maxMaterials = 256;

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
    auto result = upload_material(m_defaultAlbedoTextureIndex, m_defaultNormalTextureIndex,
                                  m_defaultRoughnessTextureIndex, m_defaultMetallicTextureIndex,
                                  m_defaultAOTextureIndex);
    if (!result)
        return make_error(result.error());
    m_defaultMaterialIndex = result.value();
    return {};
}

Result<uint32_t> Vulkan::upload_material(uint32_t albedoTextureIndex, uint32_t normalTextureIndex,
                                          uint32_t roughnessTextureIndex, uint32_t metallicTextureIndex,
                                          uint32_t aoTextureIndex)
{
    if (albedoTextureIndex >= m_textures.size() || normalTextureIndex >= m_textures.size() ||
        roughnessTextureIndex >= m_textures.size() || metallicTextureIndex >= m_textures.size() ||
        aoTextureIndex >= m_textures.size())
        return make_error("Texture index out of range", ErrorCode::AssetInvalidData);

    // Simple hash combining all 5 indices
    std::size_t materialKey = 0;
    materialKey ^= std::hash<uint32_t>{}(albedoTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<uint32_t>{}(normalTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<uint32_t>{}(roughnessTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<uint32_t>{}(metallicTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    materialKey ^= std::hash<uint32_t>{}(aoTextureIndex) + 0x9e3779b9 + (materialKey << 6) + (materialKey >> 2);
    const uint64_t key64 = static_cast<uint64_t>(materialKey);
    if (const auto it = m_materialLookup.find(key64); it != m_materialLookup.end())
        return it->second;

    VkDevice device = m_vulkanDevice.get_device();

    // Allocate descriptor set
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
    const uint32_t texIndices[5] = { albedoTextureIndex, normalTextureIndex, roughnessTextureIndex,
                                     metallicTextureIndex, aoTextureIndex };
    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

    for (uint32_t i = 0; i < 5; ++i)
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

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

    uint32_t materialIndex = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(material);
    m_materialLookup.emplace(key64, materialIndex);
    return materialIndex;
}

/// Offscreen scene rendering

void Vulkan::compute_scene_render_size()
{
    VkExtent2D swapExtent = m_swapchain.get_extent();
    m_sceneRenderWidth = std::max(1u, static_cast<uint32_t>(swapExtent.width * m_renderScale));
    m_sceneRenderHeight = std::max(1u, static_cast<uint32_t>(swapExtent.height * m_renderScale));
}

Result<VkFormat> Vulkan::find_depth_format()
{
    return find_supported_format({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                 VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

Result<VkFormat> Vulkan::find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                               VkFormatFeatureFlags features)
{
    VkPhysicalDevice physicalDevice = m_vulkanDevice.get_physical_device();

    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
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

    VkSampleCountFlags counts =
        properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;

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
        // MSAA render pass: 3 attachments
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
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
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
        // Non-MSAA render pass: 2 attachments
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
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
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
            m_sceneRenderWidth, m_sceneRenderHeight, colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_sceneColorImage, m_sceneColorMemory, VK_SAMPLE_COUNT_1_BIT,
            &sceneColorAllocBytes);
        !res)
        return res;
    m_sceneColorMemoryBytes = static_cast<uint64_t>(sceneColorAllocBytes);

    auto colorViewResult = m_vulkanDevice.create_image_view(m_sceneColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!colorViewResult)
        return make_error(colorViewResult.error());
    m_sceneColorView = colorViewResult.value();

    // Create depth image (at MSAA sample count)
    VkDeviceSize sceneDepthAllocBytes = 0;
    if (auto res = m_vulkanDevice.create_image(m_sceneRenderWidth, m_sceneRenderHeight, depthFormat,
                                               VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_sceneDepthImage,
                                               m_sceneDepthMemory, m_msaaSamples, &sceneDepthAllocBytes);
        !res)
        return res;
    m_sceneDepthMemoryBytes = static_cast<uint64_t>(sceneDepthAllocBytes);

    auto depthViewResult = m_vulkanDevice.create_image_view(m_sceneDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depthViewResult)
        return make_error(depthViewResult.error());
    m_sceneDepthView = depthViewResult.value();

    // If MSAA, create MSAA color image
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        VkDeviceSize msaaColorAllocBytes = 0;
        if (auto res = m_vulkanDevice.create_image(
                m_sceneRenderWidth, m_sceneRenderHeight, colorFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_msaaColorImage, m_msaaColorMemory, m_msaaSamples,
                &msaaColorAllocBytes);
            !res)
            return res;
        m_msaaColorMemoryBytes = static_cast<uint64_t>(msaaColorAllocBytes);

        auto msaaViewResult =
            m_vulkanDevice.create_image_view(m_msaaColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!msaaViewResult)
            return make_error(msaaViewResult.error());
        m_msaaColorView = msaaViewResult.value();
    }

    // Create framebuffer
    std::vector<VkImageView> fbAttachments;
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

    if (m_sceneFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device, m_sceneFramebuffer, nullptr);
        m_sceneFramebuffer = VK_NULL_HANDLE;
    }

    // MSAA color
    if (m_msaaColorView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_msaaColorView, nullptr);
        m_msaaColorView = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaColorMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_msaaColorMemory, nullptr);
        m_msaaColorMemory = VK_NULL_HANDLE;
    }
    m_msaaColorMemoryBytes = 0;

    // Depth
    if (m_sceneDepthView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_sceneDepthView, nullptr);
        m_sceneDepthView = VK_NULL_HANDLE;
    }
    if (m_sceneDepthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_sceneDepthImage, nullptr);
        m_sceneDepthImage = VK_NULL_HANDLE;
    }
    if (m_sceneDepthMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_sceneDepthMemory, nullptr);
        m_sceneDepthMemory = VK_NULL_HANDLE;
    }
    m_sceneDepthMemoryBytes = 0;

    // Scene color
    if (m_sceneColorView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_sceneColorView, nullptr);
        m_sceneColorView = VK_NULL_HANDLE;
    }
    if (m_sceneColorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_sceneColorImage, nullptr);
        m_sceneColorImage = VK_NULL_HANDLE;
    }
    if (m_sceneColorMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_sceneColorMemory, nullptr);
        m_sceneColorMemory = VK_NULL_HANDLE;
    }
    m_sceneColorMemoryBytes = 0;
}

void Vulkan::cleanup_scene_render_pass()
{
    VkDevice device = m_vulkanDevice.get_device();
    if (!device)
        return;

    if (m_sceneRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, m_sceneRenderPass, nullptr);
        m_sceneRenderPass = VK_NULL_HANDLE;
    }
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

    // Apply immediately so UI/renderer state stays aligned with real swapchain mode.
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

    // Clamp to max supported
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
        cleanup_scene_render_target();
        cleanup_scene_render_pass();
        m_pipeline.cleanup();

        if (auto result = create_scene_render_pass(); !result)
            return result;
        compute_scene_render_size();
        if (auto result = create_scene_render_target(); !result)
            return result;
        return m_pipeline.initialize(m_sceneRenderPass, m_msaaSamples, m_vertSpirv, m_fragSpirv);
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
}

std::int32_t Vulkan::get_msaa_samples() const noexcept
{
    return static_cast<std::int32_t>(m_msaaSamples);
}

void Vulkan::set_render_scale(float scale) noexcept
{
    scale = std::clamp(scale, 0.25f, 2.0f);
    if (std::fabs(scale - m_renderScale) < 0.0001f)
        return;

    m_renderScale = scale;
    // Defer expensive resource rebuild to draw-frame boundary.
    // This avoids mid-frame image/view churn that can cause submit failures.
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

    // Compile fresh SPIR-V from source (bypass cache — always recompile)
    auto vertResult = ShaderCompiler::compile_file(m_vertShaderPath);
    if (!vertResult)
        return make_error(vertResult.error());

    auto fragResult = ShaderCompiler::compile_file(m_fragShaderPath);
    if (!fragResult)
        return make_error(fragResult.error());

    m_vertSpirv = std::move(vertResult.value());
    m_fragSpirv = std::move(fragResult.value());

    // Rebuild pipeline with new SPIR-V
    m_pipeline.cleanup();
    return m_pipeline.initialize(m_sceneRenderPass, m_msaaSamples, m_vertSpirv, m_fragSpirv);
}
