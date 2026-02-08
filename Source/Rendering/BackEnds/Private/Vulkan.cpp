#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Public/Vulkan.hpp"
#include "../../../Core/Public/Utils.hpp"
#include "../../Public/Mesh.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <print>
#include <set>
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

/// Public methods

Result<> Vulkan::draw_frame() noexcept
{
    vkWaitForFences(this->m_device, 1, &this->m_inFlightFences[m_currentFrame], true, UINT64_MAX);

    uint32_t imageIndex{};
    VkResult result =
        vkAcquireNextImageKHR(this->m_device, this->m_swapChain, UINT64_MAX,
                              this->m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return recreate_swap_chain();
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return make_error("Failed to acquire swap chain image", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkResetFences(this->m_device, 1, &this->m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
    {
        return make_error("Failed to reset fence", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkResetCommandBuffer(this->m_commandBuffers[m_currentFrame], 0) != VK_SUCCESS)
    {
        return make_error("Failed to reset command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    auto recordResult = this->record_command_buffer(this->m_commandBuffers[m_currentFrame], imageIndex);
    if (!recordResult)
        return make_error(recordResult.error());

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {this->m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &this->m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {this->m_renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(this->m_graphicsQueue, 1, &submitInfo, this->m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
    {
        return make_error("Failed to submit draw command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {this->m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(this->m_presentQueue, &presentInfo);

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
    vkDeviceWaitIdle(this->m_device);
}

void Vulkan::cleanup() noexcept
{
    if (this->m_device)
        vkDeviceWaitIdle(this->m_device);

    if (m_myMesh.indexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, m_myMesh.indexBuffer, nullptr);
        vkFreeMemory(m_device, m_myMesh.indexBufferMemory, nullptr);
    }

    if (m_myMesh.vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, m_myMesh.vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_myMesh.vertexBufferMemory, nullptr);
    }
    // ----------------------------------

    // Depth resources are destroyed in cleanup_swap_chain()

    cleanup_swap_chain();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(this->m_device, this->m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(this->m_device, this->m_inFlightFences[i], nullptr);
    }

    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++)
    {
        vkDestroySemaphore(this->m_device, this->m_renderFinishedSemaphores[i], nullptr);
    }

    vkDestroyCommandPool(this->m_device, this->m_commandPool, nullptr);

    vkDestroyPipeline(this->m_device, this->m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(this->m_device, this->m_pipelineLayout, nullptr);
    vkDestroyRenderPass(this->m_device, this->m_renderPass, nullptr);

    vkDestroyDevice(this->m_device, nullptr);

    if (this->m_enableValidationLayers)
    {
        this->destroy_debug_utils_messenger_ext(this->m_instance, this->m_debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(this->m_instance, this->m_surface, nullptr);

    vkDestroyInstance(this->m_instance, nullptr);
}

/// Private methods

void Vulkan::framebuffer_resize_callback(GLFWwindow* window, int width, int height)
{
    auto vulkan = reinterpret_cast<Vulkan*>(glfwGetWindowUserPointer(window));
    vulkan->m_framebufferResized = true;
}

/// Debug messenger methods

Result<> Vulkan::setup_debug_messenger()
{
    if (!this->m_enableValidationLayers)
        return {};

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populate_debug_messenger_create_info(createInfo);

    if (this->create_debug_utils_messenger_ext(this->m_instance, &createInfo, nullptr, &this->m_debugMessenger) !=
        VK_SUCCESS)
    {
        return make_error("Failed to set up debug messenger", ErrorCode::VulkanDebugMessengerCreationFailed);
    }

    return {};
}

void Vulkan::populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept
{
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debug_callback;
    createInfo.pUserData = nullptr;
}

bool Vulkan::check_validation_layer_support() const noexcept
{
    uint32_t layerCount{};
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers{layerCount};
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layerName : this->m_validationLayers)
    {
        bool layerFound{false};

        for (const auto& layerProperties : availableLayers)
        {
            if (std::strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
        {
            return false;
        }
    }

    return true;
}

std::vector<const char*> Vulkan::get_required_extensions()
{
    uint32_t glfwExtensionCount{};
    const char** glfwExtensions{};
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions{glfwExtensions, glfwExtensions + glfwExtensionCount};

    if (this->m_enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

VKAPI_ATTR uint32_t VKAPI_CALL Vulkan::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData)
{
    fmt::print("INFO: validation layer => {}\n", pCallbackData->pMessage);

    return VK_FALSE;
}

VkResult Vulkan::create_debug_utils_messenger_ext(VkInstance instance,
                                                  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkDebugUtilsMessengerEXT* pDebugMessenger) noexcept
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void Vulkan::destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                               const VkAllocationCallbacks* pAllocator) noexcept
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

/// Vulkan setup methods

Result<> Vulkan::create_instance()
{
    if (this->m_enableValidationLayers && !this->check_validation_layer_support())
    {
        return make_error("Validation layers requested, but not available",
                          ErrorCode::VulkanValidationLayersNotSupported);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "NatureOfCraft";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = get_required_extensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (this->m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(this->m_validationLayers.size());
        createInfo.ppEnabledLayerNames = this->m_validationLayers.data();

        populate_debug_messenger_create_info(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &this->m_instance) != VK_SUCCESS)
    {
        return make_error("Failed to create Vulkan instance", ErrorCode::VulkanInstanceCreationFailed);
    }

    return {};
}

Result<> Vulkan::pick_physical_device()
{
    uint32_t deviceCount{};
    vkEnumeratePhysicalDevices(this->m_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        return make_error("Failed to find GPUs with Vulkan support", ErrorCode::VulkanPhysicalDeviceSelectionFailed);
    }

    std::vector<VkPhysicalDevice> devices{deviceCount};
    vkEnumeratePhysicalDevices(this->m_instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
        if (this->is_device_suitable(device))
        {
            this->m_physicalDevice = device;
            break;
        }
    }

    if (this->m_physicalDevice == VK_NULL_HANDLE)
    {
        return make_error("Failed to find a suitable GPU", ErrorCode::VulkanPhysicalDeviceSelectionFailed);
    }

    return {};
}

bool Vulkan::is_device_suitable(VkPhysicalDevice device) noexcept
{
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    QueueFamilyIndices indices = find_queue_families(device);

    bool extensionsSupported = check_device_extension_support(device);

    bool swapChainAdequate{false};
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = query_swap_chain_support(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    bool suitable = indices.is_complete() && extensionsSupported && swapChainAdequate &&
                    deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (suitable)
        fmt::print("Selected GPU: {}\n", deviceProperties.deviceName);

    return suitable;
}

bool Vulkan::check_device_extension_support(VkPhysicalDevice device) noexcept
{
    uint32_t extensionCount{};
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions{extensionCount};
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions{this->m_deviceExtensions.begin(), this->m_deviceExtensions.end()};

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

Result<> Vulkan::create_logical_device()
{
    QueueFamilyIndices indices = find_queue_families(this->m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
    std::set<uint32_t> uniqueQueueFamilies{
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
    };

    float queuePriority{1.0f};
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(this->m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = this->m_deviceExtensions.data();

    if (this->m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(this->m_validationLayers.size());
        createInfo.ppEnabledLayerNames = this->m_validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(this->m_physicalDevice, &createInfo, nullptr, &this->m_device) != VK_SUCCESS)
    {
        return make_error("Failed to create logical device", ErrorCode::VulkanLogicalDeviceCreationFailed);
    }

    vkGetDeviceQueue(this->m_device, indices.graphicsFamily.value(), 0, &this->m_graphicsQueue);
    vkGetDeviceQueue(this->m_device, indices.presentFamily.value(), 0, &this->m_presentQueue);

    return {};
}

QueueFamilyIndices Vulkan::find_queue_families(VkPhysicalDevice device) noexcept
{
    QueueFamilyIndices indices{};

    uint32_t queueFamilyCount{};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies{queueFamilyCount};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int32_t i{};
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        uint32_t presentSupport{false};
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, this->m_surface, &presentSupport);

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.is_complete())
        {
            break;
        }

        i++;
    }

    return indices;
}

Result<> Vulkan::create_surface(GLFWwindow* window) noexcept
{
    if (glfwCreateWindowSurface(this->m_instance, window, nullptr, &this->m_surface) != VK_SUCCESS)
    {
        return make_error("Failed to create window surface", ErrorCode::VulkanSurfaceCreationFailed);
    }

    return {};
}

SwapChainSupportDetails Vulkan::query_swap_chain_support(VkPhysicalDevice device) noexcept
{
    SwapChainSupportDetails details{};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, this->m_surface, &details.capabilities);

    uint32_t formatCount{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount{};
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Vulkan::choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& availableFormats) noexcept
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR Vulkan::choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availablePresentModes) noexcept
{
    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Vulkan::choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) noexcept
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    else
    {
        int width, height;
        glfwGetFramebufferSize(this->m_window, &width, &height);

        VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

        actualExtent.width =
            std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height =
            std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

Result<> Vulkan::create_swap_chain()
{
    SwapChainSupportDetails swapChainSupport = query_swap_chain_support(this->m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = choose_swap_surface_format(swapChainSupport.formats);
    VkPresentModeKHR presentMode = choose_swap_present_mode(swapChainSupport.presentModes);
    VkExtent2D extent = choose_swap_extent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = this->m_surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = find_queue_families(this->m_physicalDevice);
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
    };

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;     // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = true;

    createInfo.oldSwapchain = nullptr;

    if (vkCreateSwapchainKHR(this->m_device, &createInfo, nullptr, &this->m_swapChain) != VK_SUCCESS)
    {
        return make_error("Failed to create swap chain", ErrorCode::VulkanSwapChainCreationFailed);
    }

    vkGetSwapchainImagesKHR(this->m_device, this->m_swapChain, &imageCount, nullptr);
    this->m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(this->m_device, this->m_swapChain, &imageCount, this->m_swapChainImages.data());

    this->m_swapChainImageFormat = surfaceFormat.format;
    this->m_swapChainExtent = extent;

    return {};
}

void Vulkan::cleanup_swap_chain()
{
    if (m_depthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }

    for (auto& framebuffer : this->m_swapChainFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(this->m_device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    this->m_swapChainFramebuffers.clear();

    for (auto& imageView : this->m_swapChainImageViews)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(this->m_device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }
    }
    this->m_swapChainImageViews.clear();

    if (this->m_swapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(this->m_device, this->m_swapChain, nullptr);
        this->m_swapChain = VK_NULL_HANDLE;
    }

    this->m_swapChainImages.clear();
}

Result<> Vulkan::recreate_swap_chain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(this->m_device);

    // Destroy old per-image semaphores
    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++)
    {
        vkDestroySemaphore(this->m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    m_renderFinishedSemaphores.clear();

    this->cleanup_swap_chain();

    if (auto result = this->create_swap_chain(); !result)
        return result;
    if (auto result = this->create_image_views(); !result)
        return result;

    if (auto result = this->create_depth_resources(); !result)
        return result;

    if (auto result = this->create_framebuffers(); !result)
        return result;

    // Recreate per-image sync objects for the new swapchain
    m_renderFinishedSemaphores.resize(m_swapChainImages.size());
    m_imagesInFlight.clear();
    m_imagesInFlight.resize(m_swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < m_swapChainImages.size(); i++)
    {
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
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

Result<> Vulkan::create_image_views()
{
    this->m_swapChainImageViews.resize(this->m_swapChainImages.size());

    for (size_t i = 0; i < this->m_swapChainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = this->m_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = this->m_swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(this->m_device, &createInfo, nullptr, &this->m_swapChainImageViews[i]) != VK_SUCCESS)
        {
            return make_error("Failed to create image views", ErrorCode::VulkanImageViewCreationFailed);
        }
    }

    return {};
}

Result<> Vulkan::create_graphics_pipeline()
{
    // 1. Load Shaders
    auto vertShaderCodeResult = read_file("Resources/vert.spv");
    if (!vertShaderCodeResult)
        return make_error(vertShaderCodeResult.error());
    auto vertShaderCode = vertShaderCodeResult.value();

    auto fragShaderCode = read_file("Resources/frag.spv");
    if (!fragShaderCode)
        return make_error(fragShaderCode.error());
    auto fragShaderCodeValue = fragShaderCode.value();

    auto vertShaderModuleResult = this->create_shader_module(vertShaderCode);
    if (!vertShaderModuleResult)
        return make_error(vertShaderModuleResult.error());
    VkShaderModule vertShaderModule = vertShaderModuleResult.value();

    auto fragShaderModuleResult = this->create_shader_module(fragShaderCodeValue);
    if (!fragShaderModuleResult)
        return make_error(fragShaderModuleResult.error());
    VkShaderModule fragShaderModule = fragShaderModuleResult.value();

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
        vertShaderStageInfo,
        fragShaderStageInfo,
    };

    // 2. [FIX] Vertex Input State (Connects your Mesh to the Pipeline)
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription; // <--- ADDED
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data(); // <--- ADDED

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = false;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = false;
    rasterizer.rasterizerDiscardEnable = false;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = false;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = false;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = false;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(XMFLOAT4X4);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;              
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(this->m_device, &pipelineLayoutInfo, nullptr, &this->m_pipelineLayout) != VK_SUCCESS)
    {
        vkDestroyShaderModule(this->m_device, fragShaderModule, nullptr);
        vkDestroyShaderModule(this->m_device, vertShaderModule, nullptr);
        return make_error("Failed to create pipeline layout", ErrorCode::VulkanGraphicsPipelineLayoutCreationFailed);
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = this->m_pipelineLayout;
    pipelineInfo.renderPass = this->m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(this->m_device, nullptr, 1, &pipelineInfo, nullptr, &this->m_graphicsPipeline) !=
        VK_SUCCESS)
    {
        vkDestroyShaderModule(this->m_device, fragShaderModule, nullptr);
        vkDestroyShaderModule(this->m_device, vertShaderModule, nullptr);
        return make_error("Failed to create graphics pipeline", ErrorCode::VulkanGraphicsPipelineCreationFailed);
    }

    vkDestroyShaderModule(this->m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(this->m_device, vertShaderModule, nullptr);
    return {};
}

Result<VkShaderModule> Vulkan::create_shader_module(const std::vector<char>& code) noexcept
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(this->m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        return make_error("Failed to create shader module", ErrorCode::VulkanShaderModuleCreationFailed);
    }

    return shaderModule;
}

Result<> Vulkan::create_render_pass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = this->m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    auto depthFormatResult = find_depth_format();
    if (!depthFormatResult)
        return make_error(depthFormatResult.error());
    depthAttachment.format = depthFormatResult.value();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(this->m_device, &renderPassInfo, nullptr, &this->m_renderPass) != VK_SUCCESS)
    {
        return make_error("Failed to create render pass", ErrorCode::VulkanRenderPassCreationFailed);
    }

    return {};
}

Result<> Vulkan::create_framebuffers()
{
    this->m_swapChainFramebuffers.resize(this->m_swapChainImageViews.size());
    for (size_t i = 0; i < this->m_swapChainImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {this->m_swapChainImageViews[i], m_depthImageView};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = this->m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = this->m_swapChainExtent.width;
        framebufferInfo.height = this->m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(this->m_device, &framebufferInfo, nullptr, &this->m_swapChainFramebuffers[i]) !=
            VK_SUCCESS)
        {
            return make_error("Failed to create framebuffer", ErrorCode::VulkanFramebufferCreationFailed);
        }
    }
    return {};
}

Result<> Vulkan::create_command_pool()
{
    QueueFamilyIndices queueFamilyIndices = find_queue_families(this->m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(this->m_device, &poolInfo, nullptr, &this->m_commandPool) != VK_SUCCESS)
    {
        return make_error("Failed to create command pool", ErrorCode::VulkanCommandPoolCreationFailed);
    }

    return {};
}

Result<> Vulkan::create_command_buffers()
{
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(this->m_device, &allocInfo, this->m_commandBuffers.data()) != VK_SUCCESS)
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
    renderPassInfo.renderPass = this->m_renderPass;
    renderPassInfo.framebuffer = this->m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = this->m_swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(this->m_swapChainExtent.width);
    viewport.height = static_cast<float>(this->m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = this->m_swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    // Rotate around Y axis for turntable effect
    XMMATRIX model = XMMatrixRotationY(time * 0.5f);

    // Position
    XMVECTOR eyePos = XMVectorSet(0.0f, 3.0f, 10.0f, 0.0f);
    XMVECTOR focusPos = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR upDir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtRH(eyePos, focusPos, upDir);

    // Projection
    float aspectRatio = this->m_swapChainExtent.width / (float)this->m_swapChainExtent.height;
    XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(45.0f), aspectRatio, 0.1f, 1000.0f);

    // Vulkan Clip Space (Y-flip)
    XMMATRIX vulkanFix = XMMatrixScaling(1.0f, -1.0f, 1.0f);
    proj = XMMatrixMultiply(proj, vulkanFix);

    // Calculate MVP (Row-Major for DX, interpreted as Transpose by GLSL)
    XMMATRIX mvp = XMMatrixMultiply(model, view);
    mvp = XMMatrixMultiply(mvp, proj);

    XMFLOAT4X4 mvpData;
    XMStoreFloat4x4(&mvpData, mvp);

    vkCmdPushConstants(commandBuffer, this->m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(XMFLOAT4X4),
                       &mvpData);

    VkBuffer vertexBuffers[] = {this->m_myMesh.vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer, this->m_myMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, this->m_myMesh.indexCount, 1, 0, 0, 0);

    if (this->get_ui_render_callback())
    {
        this->get_ui_render_callback()(commandBuffer);
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
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    m_renderFinishedSemaphores.resize(m_swapChainImages.size());
    m_imagesInFlight.resize(m_swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &this->m_imageAvailableSemaphores[i]) !=
                VK_SUCCESS ||
            vkCreateFence(this->m_device, &fenceInfo, nullptr, &this->m_inFlightFences[i]) != VK_SUCCESS)
        {
            return make_error("Failed to create synchronization objects for a frame",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    for (size_t i = 0; i < m_swapChainImages.size(); i++)
    {
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &this->m_renderFinishedSemaphores[i]) !=
            VK_SUCCESS)
        {
            return make_error("Failed to create render finished semaphore", ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    return {};
}

Result<uint32_t> Vulkan::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return make_error("Failed to find suitable memory type", ErrorCode::VulkanMemoryTypeMissing);
}

Result<> Vulkan::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                               VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        return make_error("Failed to create buffer", ErrorCode::VulkanBufferCreationFailed);
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    auto memTypeResult = find_memory_type(memRequirements.memoryTypeBits, properties);
    if (!memTypeResult)
    {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return make_error(memTypeResult.error());
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return make_error("Failed to allocate buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }

    if (vkBindBufferMemory(m_device, buffer, bufferMemory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, bufferMemory, nullptr);
        vkDestroyBuffer(m_device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
        return make_error("Failed to bind buffer memory", ErrorCode::VulkanBufferCreationFailed);
    }

    return {};
}

Result<Mesh> Vulkan::load_model(std::string_view filename)
{
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

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    VkBuffer stagingBuffer{};
    VkDeviceMemory stagingBufferMemory{};

    if (auto result = create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    stagingBuffer, stagingBufferMemory);
        !result)
        return make_error(result.error());

    void* data{};
    if (vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, 0, &data) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error("Failed to map vertex staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(m_device, stagingBufferMemory);

    if (auto result = create_buffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer, mesh.vertexBufferMemory);
        !result)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error(result.error());
    }

    if (auto result = copy_buffer(stagingBuffer, mesh.vertexBuffer, bufferSize); !result)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error(result.error());
    }

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
    if (auto result = create_buffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    stagingBuffer, stagingBufferMemory);
        !result)
        return make_error(result.error());

    if (vkMapMemory(m_device, stagingBufferMemory, 0, indexBufferSize, 0, &data) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error("Failed to map index staging buffer memory", ErrorCode::VulkanMemoryAllocationFailed);
    }
    memcpy(data, indices.data(), (size_t)indexBufferSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    if (auto result =
            create_buffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer, mesh.indexBufferMemory);
        !result)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error(result.error());
    }

    if (auto result = copy_buffer(stagingBuffer, mesh.indexBuffer, indexBufferSize); !result)
    {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingBufferMemory, nullptr);
        return make_error(result.error());
    }

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);

    return mesh;
}

Result<> Vulkan::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        return make_error("Failed to allocate command buffer for copy", ErrorCode::VulkanCopyBufferFailed);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to begin command buffer for copy", ErrorCode::VulkanCopyBufferFailed);
    }

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to end command buffer for copy", ErrorCode::VulkanCopyBufferFailed);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to submit copy command", ErrorCode::VulkanCopyBufferFailed);
    }

    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
    return {};
}

Result<VkFormat> Vulkan::find_supported_format(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                               VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

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

Result<VkFormat> Vulkan::find_depth_format()
{
    return find_supported_format({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                 VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool Vulkan::has_stencil_component(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

Result<> Vulkan::create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                              VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                              VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        return make_error("Failed to create image", ErrorCode::VulkanImageCreationFailed);
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    auto memTypeResult = find_memory_type(memRequirements.memoryTypeBits, properties);
    if (!memTypeResult)
    {
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
        return make_error(memTypeResult.error());
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memTypeResult.value();

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
        return make_error("Failed to allocate image memory", ErrorCode::VulkanMemoryAllocationFailed);
    }

    if (vkBindImageMemory(m_device, image, imageMemory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
        imageMemory = VK_NULL_HANDLE;
        return make_error("Failed to bind image memory", ErrorCode::VulkanImageCreationFailed);
    }

    return {};
}

Result<VkImageView> Vulkan::create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        return make_error("Failed to create image view", ErrorCode::VulkanImageViewCreationFailed);
    }

    return imageView;
}

Result<> Vulkan::create_depth_resources()
{
    auto depthFormatResult = find_depth_format();
    if (!depthFormatResult)
        return make_error(depthFormatResult.error());

    VkFormat depthFormat = depthFormatResult.value();

    if (auto result = create_image(m_swapChainExtent.width, m_swapChainExtent.height, depthFormat,
                                   VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthImageMemory);
        !result)
    {
        return result;
    }

    auto imageViewResult = create_image_view(m_depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!imageViewResult)
        return make_error(imageViewResult.error());

    m_depthImageView = imageViewResult.value();
    return {};
}