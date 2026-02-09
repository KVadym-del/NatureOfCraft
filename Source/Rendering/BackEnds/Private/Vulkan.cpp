#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Public/Vulkan.hpp"
#include "../../Public/Mesh.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>

#include <imgui_impl_vulkan.h>

#include <rapidobj/rapidobj.hpp>

#include <DirectXMath.h>
using namespace DirectX;

namespace std
{
template <> struct hash<Vertex>
{
    size_t operator()(Vertex const& vertex) const
    {
        size_t h1 =
            hash<float>()(vertex.pos.x) ^ (hash<float>()(vertex.pos.y) << 1) ^ (hash<float>()(vertex.pos.z) << 2);
        size_t h2 = hash<float>()(vertex.normal.x) ^ (hash<float>()(vertex.normal.y) << 1) ^
                    (hash<float>()(vertex.normal.z) << 2);
        size_t h3 = hash<float>()(vertex.texCoord.x) ^ (hash<float>()(vertex.texCoord.y) << 1);
        return h1 ^ (h2 << 1) ^ (h3 << 1);
    }
};
} // namespace std

/// Initialization

Result<> Vulkan::initialize() noexcept
{
    if (auto result = m_vulkanDevice.initialize(); !result)
        return result;
    if (auto result = m_swapchain.initialize(); !result)
        return result;
    if (auto result = m_pipeline.initialize(m_swapchain.get_render_pass()); !result)
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

    // Sub-components clean themselves up in reverse declaration order via destructors:
    //   ~m_pipeline -> ~m_swapchain -> ~m_vulkanDevice
    // Their destructors call their own cleanup() methods.
    // We explicitly clean them here to control ordering before the Vulkan class destructor runs.
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

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_swapchain.get_render_pass();
    renderPassInfo.framebuffer = m_swapchain.get_framebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain.get_extent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.get_pipeline());

    VkExtent2D extent = m_swapchain.get_extent();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
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

        XMFLOAT4X4 mvpData;
        XMStoreFloat4x4(&mvpData, mvp);

        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(XMFLOAT4X4), &mvpData);

        VkBuffer vertexBuffers[] = {mesh.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    if (get_ui_render_callback())
    {
        get_ui_render_callback()(commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);

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

/// Model loading

Result<uint32_t> Vulkan::load_model(std::string_view filename)
{
    VkDevice device = m_vulkanDevice.get_device();

    Mesh mesh{};
    std::filesystem::path filePath(filename);
    rapidobj::Result result =
        rapidobj::ParseFile(filePath, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
    if (result.error)
        return make_error(result.error.code.message());

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    const auto& attrib = result.attributes;
    const auto numPositions = attrib.positions.size() / 3;
    const auto numTexCoords = attrib.texcoords.size() / 2;
    const auto numNormals = attrib.normals.size() / 3;

    auto makeVertex = [&](const rapidobj::Index& index) -> Vertex {
        Vertex vertex{};

        vertex.pos = {attrib.positions[3 * index.position_index + 0], attrib.positions[3 * index.position_index + 1],
                      attrib.positions[3 * index.position_index + 2]};

        if (index.texcoord_index >= 0 && static_cast<size_t>(index.texcoord_index) < numTexCoords)
        {
            vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0],
                               1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};
        }

        if (index.normal_index >= 0 && static_cast<size_t>(index.normal_index) < numNormals)
        {
            vertex.normal = {attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1],
                             attrib.normals[3 * index.normal_index + 2]};
        }
        else
        {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        }

        return vertex;
    };

    auto addVertex = [&](const Vertex& vertex) {
        if (uniqueVertices.count(vertex) == 0)
        {
            uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
            vertices.push_back(vertex);
        }
        indices.push_back(uniqueVertices[vertex]);
    };

    for (const auto& shape : result.shapes)
    {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const auto numFaceVerts = static_cast<size_t>(shape.mesh.num_face_vertices[f]);

            bool faceValid = numFaceVerts >= 3;
            if (faceValid)
            {
                for (size_t v = 0; v < numFaceVerts; ++v)
                {
                    const auto& idx = shape.mesh.indices[indexOffset + v];
                    if (idx.position_index < 0 || static_cast<size_t>(idx.position_index) >= numPositions)
                    {
                        faceValid = false;
                        break;
                    }
                }
            }

            if (faceValid)
            {
                const Vertex v0 = makeVertex(shape.mesh.indices[indexOffset]);
                for (size_t v = 1; v + 1 < numFaceVerts; ++v)
                {
                    const Vertex v1 = makeVertex(shape.mesh.indices[indexOffset + v]);
                    const Vertex v2 = makeVertex(shape.mesh.indices[indexOffset + v + 1]);
                    addVertex(v0);
                    addVertex(v1);
                    addVertex(v2);
                }
            }

            indexOffset += numFaceVerts;
        }
    }

    if (vertices.empty())
        return make_error("Model has no valid vertices");

    mesh.indexCount = static_cast<uint32_t>(indices.size());

    // --- Vertex buffer ---
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
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
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
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
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
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
    memcpy(data, indices.data(), (size_t)indexBufferSize);
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
