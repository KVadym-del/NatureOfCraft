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
#include <cstdint>
#include <cstring>
#include <vector>

#include <GLFW/glfw3.h>

#include <imgui_impl_vulkan.h>

#include <DirectXMath.h>
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

Result<> Vulkan::draw_frame() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();

    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], true, UINT64_MAX);

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

    if (vkQueueSubmit(m_vulkanDevice.get_graphics_queue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) !=
        VK_SUCCESS)
    {
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

void Vulkan::cleanup() noexcept
{
    VkDevice device = m_vulkanDevice.get_device();

    if (device)
        vkDeviceWaitIdle(device);

    // Destroy meshes (use device for vkDestroyBuffer/vkFreeMemory)
    for (auto& mesh : m_meshes)
    {
        if (mesh.indexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
            vkFreeMemory(device, mesh.indexBufferMemory, nullptr);
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
            vkFreeMemory(device, mesh.vertexBufferMemory, nullptr);
        }
    }
    m_meshes.clear();

    // Destroy textures
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

    // Destroy descriptor pool (frees all descriptor sets implicitly)
    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_materials.clear();

    // Destroy sampler
    if (m_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

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

    // Destroy offscreen resources
    cleanup_scene_render_target();
    cleanup_scene_render_pass();

    // Sub-components clean up
    m_pipeline.cleanup();
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

Result<> Vulkan::record_command_buffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) noexcept
{
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

        // Clear values: color + depth (+ MSAA color if applicable, but clear order matches attachment order)
        std::vector<VkClearValue> clearValues;
        if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
        {
            // Attachments: 0=MSAA color, 1=MSAA depth, 2=resolve color
            clearValues.resize(3);
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
            clearValues[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        }
        else
        {
            // Attachments: 0=color, 1=depth
            clearValues.resize(2);
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};
        }

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

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

        // Use view/projection matrices set externally by the camera
        XMMATRIX view = XMLoadFloat4x4(&m_viewMatrix);
        XMMATRIX proj = XMLoadFloat4x4(&m_projMatrix);
        XMMATRIX viewProj = XMMatrixMultiply(view, proj);

        VkPipelineLayout pipelineLayout = m_pipeline.get_pipeline_layout();

        // Draw all renderables
        for (const auto& renderable : m_renderables)
        {
            if (renderable.meshIndex >= m_meshes.size())
                continue;

            const auto& mesh = m_meshes[renderable.meshIndex];
            if (mesh.vertexBuffer == VK_NULL_HANDLE || mesh.indexBuffer == VK_NULL_HANDLE)
                continue;

            XMMATRIX world = XMLoadFloat4x4(&renderable.worldMatrix);
            XMMATRIX mvp = XMMatrixMultiply(world, viewProj);

            // Push constants: MVP (offset 0) + Model (offset 64)
            struct PushConstants
            {
                XMFLOAT4X4 mvp;
                XMFLOAT4X4 model;
            } pushData;
            XMStoreFloat4x4(&pushData.mvp, mvp);
            pushData.model = renderable.worldMatrix;

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants),
                               &pushData);

            // Bind material descriptor set
            uint32_t matIdx = renderable.materialIndex;
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

            VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    // ==========================================
    // Blit: scene color -> swapchain image
    // ==========================================
    {
        VkImage swapchainImage = m_swapchain.get_images()[imageIndex];

        // Transition swapchain image: UNDEFINED -> TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapchainImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);

        VkExtent2D swapExtent = m_swapchain.get_extent();

        VkImageBlit blitRegion{};
        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.mipLevel = 0;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {static_cast<int32_t>(m_sceneRenderWidth), static_cast<int32_t>(m_sceneRenderHeight),
                                    1};

        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.mipLevel = 0;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstOffsets[0] = {0, 0, 0};
        blitRegion.dstOffsets[1] = {static_cast<int32_t>(swapExtent.width), static_cast<int32_t>(swapExtent.height), 1};

        // Scene color image is already TRANSFER_SRC_OPTIMAL from scene render pass finalLayout
        vkCmdBlitImage(commandBuffer, m_sceneColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImage,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blitRegion, VK_FILTER_LINEAR);
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
        // No clear — loadOp is LOAD
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            return make_error("Failed to create synchronization objects for a frame",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    for (size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            return make_error("Failed to create render finished semaphore", ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    return {};
}

Result<> Vulkan::recreate_swap_chain()
{
    GLFWwindow* window = m_vulkanDevice.get_window();
    VkDevice device = m_vulkanDevice.get_device();

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    // Destroy old per-image semaphores
    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++)
    {
        vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_renderFinishedSemaphores.clear();

    if (auto result = m_swapchain.recreate(); !result)
        return result;

    // Recreate offscreen scene render target (size may have changed)
    cleanup_scene_render_target();
    compute_scene_render_size();
    if (auto result = create_scene_render_target(); !result)
        return result;

    // Recreate per-image sync objects for the new swapchain
    uint32_t imageCount = m_swapchain.get_image_count();
    m_renderFinishedSemaphores.resize(imageCount);
    m_imagesInFlight.clear();
    m_imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < imageCount; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            return make_error("Failed to recreate render finished semaphore",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

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

    VkDevice device = m_vulkanDevice.get_device();
    Mesh mesh{};
    mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());

    // --- Vertex buffer ---
    VkDeviceSize bufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
    VkBuffer stagingBuffer{};
    VkDeviceMemory stagingBufferMemory{};

    if (auto res =
            m_vulkanDevice.create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         stagingBuffer, stagingBufferMemory);
        !res)
        return make_error(res.error());

    void* data{};
    if (vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error("Failed to map vertex staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }
    memcpy(data, meshData.vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    if (auto res = m_vulkanDevice.create_buffer(
            bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexBufferMemory);
        !res)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    if (auto res = m_vulkanDevice.copy_buffer(stagingBuffer, mesh.vertexBuffer, bufferSize); !res)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // --- Index buffer ---
    VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();
    if (auto res =
            m_vulkanDevice.create_buffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         stagingBuffer, stagingBufferMemory);
        !res)
        return make_error(res.error());

    if (vkMapMemory(device, stagingBufferMemory, 0, indexBufferSize, 0, &data) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error("Failed to map index staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }
    memcpy(data, meshData.indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    if (auto res = m_vulkanDevice.create_buffer(
            indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexBufferMemory);
        !res)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    if (auto res = m_vulkanDevice.copy_buffer(stagingBuffer, mesh.indexBuffer, indexBufferSize); !res)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    uint32_t meshIndex = static_cast<uint32_t>(m_meshes.size());
    m_meshes.push_back(mesh);
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

    return {};
}

Result<uint32_t> Vulkan::upload_texture(const TextureData& textureData)
{
    if (textureData.pixels.empty() || textureData.width == 0 || textureData.height == 0)
        return make_error("TextureData has no pixel data", ErrorCode::AssetInvalidData);

    VkDevice device = m_vulkanDevice.get_device();
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(textureData.width) * textureData.height * 4;

    // Create staging buffer
    VkBuffer stagingBuffer{};
    VkDeviceMemory stagingBufferMemory{};

    if (auto res =
            m_vulkanDevice.create_buffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         stagingBuffer, stagingBufferMemory);
        !res)
        return make_error(res.error());

    // Copy pixel data to staging buffer
    void* data{};
    if (vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error("Failed to map texture staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }
    memcpy(data, textureData.pixels.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Create the VkImage
    GpuTexture texture{};
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    if (auto res = m_vulkanDevice.create_image(textureData.width, textureData.height, format, VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture.image, texture.memory);
        !res)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    // Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
    if (auto res = m_vulkanDevice.transition_image_layout(texture.image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        !res)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    // Copy staging buffer to image
    if (auto res =
            m_vulkanDevice.copy_buffer_to_image(stagingBuffer, texture.image, textureData.width, textureData.height);
        !res)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    // Transition: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    if (auto res = m_vulkanDevice.transition_image_layout(texture.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        !res)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return make_error(res.error());
    }

    // Staging buffer no longer needed
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // Create image view
    auto viewResult = m_vulkanDevice.create_image_view(texture.image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!viewResult)
    {
        vkDestroyImage(device, texture.image, nullptr);
        vkFreeMemory(device, texture.memory, nullptr);
        return make_error(viewResult.error());
    }
    texture.imageView = viewResult.value();

    uint32_t textureIndex = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(texture);
    return textureIndex;
}

/// Descriptor pool and materials

Result<> Vulkan::create_descriptor_pool()
{
    VkDevice device = m_vulkanDevice.get_device();

    // Pool sized for up to 256 materials, each needing 2 combined image samplers
    constexpr uint32_t maxMaterials = 256;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxMaterials * 2;

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
    auto result = upload_material(m_defaultAlbedoTextureIndex, m_defaultNormalTextureIndex);
    if (!result)
        return make_error(result.error());
    m_defaultMaterialIndex = result.value();
    return {};
}

Result<uint32_t> Vulkan::upload_material(uint32_t albedoTextureIndex, uint32_t normalTextureIndex)
{
    if (albedoTextureIndex >= m_textures.size() || normalTextureIndex >= m_textures.size())
        return make_error("Texture index out of range", ErrorCode::AssetInvalidData);

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

    // Write descriptor set: binding 0 = albedo, binding 1 = normal
    std::array<VkDescriptorImageInfo, 2> imageInfos{};
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[0].imageView = m_textures[albedoTextureIndex].imageView;
    imageInfos[0].sampler = m_sampler;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfos[1].imageView = m_textures[normalTextureIndex].imageView;
    imageInfos[1].sampler = m_sampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = material.descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfos[0];

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = material.descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfos[1];

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

    uint32_t materialIndex = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(material);
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
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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
        // 0: Color (samples=1, CLEAR, STORE, finalLayout=TRANSFER_SRC)
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
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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

    auto depthFormatResult = find_depth_format();
    if (!depthFormatResult)
        return make_error(depthFormatResult.error());
    VkFormat depthFormat = depthFormatResult.value();

    // Always create the resolve/final color image (1x sample, TRANSFER_SRC for blit)
    if (auto res =
            m_vulkanDevice.create_image(m_sceneRenderWidth, m_sceneRenderHeight, colorFormat, VK_IMAGE_TILING_OPTIMAL,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_sceneColorImage, m_sceneColorMemory);
        !res)
        return res;

    auto colorViewResult = m_vulkanDevice.create_image_view(m_sceneColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!colorViewResult)
        return make_error(colorViewResult.error());
    m_sceneColorView = colorViewResult.value();

    // Create depth image (at MSAA sample count)
    if (auto res = m_vulkanDevice.create_image(m_sceneRenderWidth, m_sceneRenderHeight, depthFormat,
                                               VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_sceneDepthImage,
                                               m_sceneDepthMemory, m_msaaSamples);
        !res)
        return res;

    auto depthViewResult = m_vulkanDevice.create_image_view(m_sceneDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!depthViewResult)
        return make_error(depthViewResult.error());
    m_sceneDepthView = depthViewResult.value();

    // If MSAA, create MSAA color image
    if (m_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        if (auto res = m_vulkanDevice.create_image(
                m_sceneRenderWidth, m_sceneRenderHeight, colorFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_msaaColorImage, m_msaaColorMemory, m_msaaSamples);
            !res)
            return res;

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
        return;

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
    if (m_vsyncEnabled && mode == KHR_Settings::VSync)
        return;

    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    if (mode == KHR_Settings::VSync)
    {
        m_vsyncEnabled = true;
        m_swapchain.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    }
    else if (mode == KHR_Settings::Triple_Buffering)
    {
        m_vsyncEnabled = false;
        // Prefer mailbox (triple buffering), will fall back to auto-select
        m_swapchain.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
    }
    else if (mode == KHR_Settings::Immediate)
    {
        m_vsyncEnabled = false;
        m_swapchain.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    }
    // Swapchain will pick up the new present mode on next recreate
    m_framebufferResized = true; // Forces swapchain recreation
}

bool Vulkan::get_vsync() const noexcept
{
    return m_vsyncEnabled;
}

void Vulkan::set_msaa_samples(int samples) noexcept
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

    m_msaaSamples = desired;
    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    // Need to recreate: scene render pass, scene render target, pipeline
    cleanup_scene_render_target();
    cleanup_scene_render_pass();
    m_pipeline.cleanup();

    // Recreate in order
    (void)create_scene_render_pass();
    compute_scene_render_size();
    (void)create_scene_render_target();
    (void)m_pipeline.initialize(m_sceneRenderPass, m_msaaSamples, m_vertSpirv, m_fragSpirv);
}

int Vulkan::get_msaa_samples() const noexcept
{
    return static_cast<int>(m_msaaSamples);
}

void Vulkan::set_render_scale(float scale) noexcept
{
    scale = std::clamp(scale, 0.25f, 2.0f);
    if (scale == m_renderScale)
        return;

    m_renderScale = scale;
    VkDevice device = m_vulkanDevice.get_device();
    vkDeviceWaitIdle(device);

    // Only need to recreate scene render target (render pass is unchanged)
    cleanup_scene_render_target();
    compute_scene_render_size();
    (void)create_scene_render_target();
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
