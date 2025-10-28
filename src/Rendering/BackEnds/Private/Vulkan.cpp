#include "../Public/Vulkan.hpp"

#include <cstdint>
#include <cstring>
#include <print>
#include <set>
#include <vector>

#include <GLFW/glfw3.h>

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

    bool suitable = indices.is_complete() && (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) &&
                    extensionsSupported && swapChainAdequate;
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
    // Placeholder for graphics pipeline creation
    return {};
}

void Vulkan::cleanup() noexcept
{
    for (auto imageView : this->m_swapChainImageViews)
    {
        vkDestroyImageView(this->m_device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(this->m_device, this->m_swapChain, nullptr);

    vkDestroyDevice(this->m_device, nullptr);

    if (this->m_enableValidationLayers)
    {
        this->destroy_debug_utils_messenger_ext(this->m_instance, this->m_debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(this->m_instance, this->m_surface, nullptr);

    vkDestroyInstance(this->m_instance, nullptr);
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
