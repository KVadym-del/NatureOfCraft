#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Public/Vulkan.hpp"

#include "../../../Core/Public/Utils.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <print>
#include <set>
#include <vector>
#include <algorithm>

#include <GLFW/glfw3.h>

/// Public methods

Result<> Vulkan::draw_frame() noexcept
{
    vkWaitForFences(this->m_device, 1, &this->m_inFlightFences[m_currentFrame], true,
                    std::numeric_limits<uint64_t>::max());

    uint32_t imageIndex{};
    VkResult result = vkAcquireNextImageKHR(this->m_device, this->m_swapChain, std::numeric_limits<uint64_t>::max(),
                                            this->m_imageAvailableSemaphores[m_currentFrame], nullptr, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (auto result = recreate_swap_chain(); !result)
            return result;
        return {};
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        return make_error("Failed to acquire swap chain image", ErrorCode::VulkanDrawFrameFailed);
    }

    // Wait for the image to be available (if a previous frame is using it)
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(this->m_device, 1, &m_imagesInFlight[imageIndex], true,
                        std::numeric_limits<uint64_t>::max());
    }
    // Mark the image as now being in use by this frame
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    vkResetFences(this->m_device, 1, &this->m_inFlightFences[m_currentFrame]);

    vkResetCommandBuffer(this->m_commandBuffers[m_currentFrame], 0);

    auto recordResult = this->record_command_buffer(this->m_commandBuffers[m_currentFrame], imageIndex);
    if (!recordResult)
        return make_error(recordResult.error());

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    std::array<VkSemaphore, 1> waitSemaphores{
        this->m_imageAvailableSemaphores[m_currentFrame],
    };
    std::array<VkPipelineStageFlags, 1> waitStages{
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &this->m_commandBuffers[m_currentFrame];

    // Use per-image semaphore indexed by the acquired image
    std::array<VkSemaphore, 1> signalSemaphores{
        this->m_renderFinishedSemaphores[imageIndex],
    };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    if (vkQueueSubmit(this->m_graphicsQueue, 1, &submitInfo, this->m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
    {
        return make_error("Failed to submit draw command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores.data();

    std::array<VkSwapchainKHR, 1> swapChains{
        this->m_swapChain,
    };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains.data();
    presentInfo.pImageIndices = &imageIndex;

    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(this->m_presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        if (auto result = recreate_swap_chain(); !result)
            return result;
        return {};
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
    cleanup_swap_chain();

    // Destroy per-frame sync objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(this->m_device, this->m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(this->m_device, this->m_inFlightFences[i], nullptr);
    }

    // Destroy per-image sync objects
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

    if (this->ceate_debug_utils_messenger_ext(this->m_instance, &createInfo, nullptr, &this->m_debugMessenger) !=
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

VkResult Vulkan::ceate_debug_utils_messenger_ext(VkInstance instance,
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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

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
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = false;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f;          // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;    // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = false;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;       // Optional
    multisampling.pSampleMask = nullptr;         // Optional
    multisampling.alphaToCoverageEnable = false; // Optional
    multisampling.alphaToOneEnable = false;      // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = false;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;             // Optional

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = false;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;            // Optional
    pipelineLayoutInfo.pSetLayouts = nullptr;         // Optional
    pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

    if (vkCreatePipelineLayout(this->m_device, &pipelineLayoutInfo, nullptr, &this->m_pipelineLayout) != VK_SUCCESS)
    {
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
    pipelineInfo.pDepthStencilState = nullptr; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = this->m_pipelineLayout;
    pipelineInfo.renderPass = this->m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr; // Optional
    pipelineInfo.basePipelineIndex = -1;       // Optional

    if (vkCreateGraphicsPipelines(this->m_device, nullptr, 1, &pipelineInfo, nullptr, &this->m_graphicsPipeline) !=
        VK_SUCCESS)
    {
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

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
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
        VkImageView attachments[] = {this->m_swapChainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = this->m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
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
    beginInfo.flags = 0;                  // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

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

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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

    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    // Call optional UI render callback (e.g., ImGui Vulkan backend)
    {
        const auto& uiCb = this->get_ui_render_callback();
        if (uiCb)
            uiCb(commandBuffer);
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
    // Per-frame sync objects
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    
    // Per-swapchain-image sync objects
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

    // Create per-image render finished semaphores
    for (size_t i = 0; i < m_swapChainImages.size(); i++)
    {
        if (vkCreateSemaphore(this->m_device, &semaphoreInfo, nullptr, &this->m_renderFinishedSemaphores[i]) !=
                VK_SUCCESS)
        {
            return make_error("Failed to create render finished semaphore",
                              ErrorCode::VulkanSyncObjectsCreationFailed);
        }
    }

    return {};
}
