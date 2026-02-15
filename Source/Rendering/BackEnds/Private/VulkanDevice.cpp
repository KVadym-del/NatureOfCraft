#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../Public/VulkanDevice.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <print>
#include <set>
#include <vector>

#include <fmt/core.h>

VulkanDevice::~VulkanDevice()
{
    cleanup();
}

Result<> VulkanDevice::initialize() noexcept
{
    if (m_window == nullptr)
    {
        return make_error("GLFW window is null", ErrorCode::VulkanGLFWWindowIsNull);
    }

    if (auto result = create_instance(); !result)
        return result;
    if (auto result = setup_debug_messenger(); !result)
        return result;
    if (auto result = create_surface(m_window); !result)
        return result;
    if (auto result = pick_physical_device(); !result)
        return result;
    if (auto result = create_logical_device(); !result)
        return result;
    if (auto result = create_command_pool(); !result)
        return result;

    return {};
}

void VulkanDevice::cleanup() noexcept
{
    if (m_commandPool)
    {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_device)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_enableValidationLayers && m_debugMessenger)
    {
        destroy_debug_utils_messenger_ext(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_surface)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    m_hasMemoryBudgetExtension = false;
    m_getPhysicalDeviceMemoryProperties2 = nullptr;
    m_getPhysicalDeviceMemoryProperties2KHR = nullptr;
    m_cachedMemoryBudget = {};
    m_hasCachedMemoryBudget = false;
    m_lastMemoryBudgetQuery = {};
}

// --- Instance / Debug Messenger ---

Result<> VulkanDevice::create_instance()
{
    if (m_enableValidationLayers && !check_validation_layer_support())
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
    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        populate_debug_messenger_create_info(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
    {
        return make_error("Failed to create Vulkan instance", ErrorCode::VulkanInstanceCreationFailed);
    }

    return {};
}

Result<> VulkanDevice::setup_debug_messenger()
{
    if (!m_enableValidationLayers)
        return {};

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populate_debug_messenger_create_info(createInfo);

    if (create_debug_utils_messenger_ext(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
    {
        return make_error("Failed to set up debug messenger", ErrorCode::VulkanDebugMessengerCreationFailed);
    }

    return {};
}

void VulkanDevice::populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept
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

bool VulkanDevice::check_validation_layer_support() const noexcept
{
    uint32_t layerCount{};
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers{layerCount};
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layerName : m_validationLayers)
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
            return false;
    }

    return true;
}

std::vector<const char*> VulkanDevice::get_required_extensions()
{
    uint32_t glfwExtensionCount{};
    const char** glfwExtensions{};
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions{glfwExtensions, glfwExtensions + glfwExtensionCount};

    if (m_enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

VKAPI_ATTR uint32_t VKAPI_CALL VulkanDevice::debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                            void* pUserData)
{
    fmt::print("INFO: validation layer => {}\n", pCallbackData->pMessage);
    return VK_FALSE;
}

VkResult VulkanDevice::create_debug_utils_messenger_ext(VkInstance instance,
                                                        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                        const VkAllocationCallbacks* pAllocator,
                                                        VkDebugUtilsMessengerEXT* pDebugMessenger) noexcept
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void VulkanDevice::destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                                     const VkAllocationCallbacks* pAllocator) noexcept
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
        func(instance, debugMessenger, pAllocator);
}

// --- Surface / Physical Device / Logical Device ---

Result<> VulkanDevice::create_surface(GLFWwindow* window) noexcept
{
    if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS)
    {
        return make_error("Failed to create window surface", ErrorCode::VulkanSurfaceCreationFailed);
    }
    return {};
}

Result<> VulkanDevice::pick_physical_device()
{
    uint32_t deviceCount{};
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        return make_error("Failed to find GPUs with Vulkan support", ErrorCode::VulkanPhysicalDeviceSelectionFailed);
    }

    std::vector<VkPhysicalDevice> devices{deviceCount};
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
        if (is_device_suitable(device))
        {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        return make_error("Failed to find a suitable GPU", ErrorCode::VulkanPhysicalDeviceSelectionFailed);
    }

    m_hasMemoryBudgetExtension = false;
    uint32_t extensionCount{};
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr);
    if (extensionCount > 0)
    {
        std::vector<VkExtensionProperties> availableExtensions{extensionCount};
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data());
        for (const auto& extension : availableExtensions)
        {
            if (std::strcmp(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
            {
                m_hasMemoryBudgetExtension = true;
                break;
            }
        }
    }

    m_getPhysicalDeviceMemoryProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
        vkGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceMemoryProperties2"));
    m_getPhysicalDeviceMemoryProperties2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2KHR>(
        vkGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceMemoryProperties2KHR"));
    m_cachedMemoryBudget = {};
    m_hasCachedMemoryBudget = false;
    m_lastMemoryBudgetQuery = {};

    return {};
}

bool VulkanDevice::is_device_suitable(VkPhysicalDevice device) noexcept
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

bool VulkanDevice::check_device_extension_support(VkPhysicalDevice device) noexcept
{
    uint32_t extensionCount{};
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions{extensionCount};
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions{m_deviceExtensions.begin(), m_deviceExtensions.end()};

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

DeviceLocalMemoryBudget VulkanDevice::get_device_local_memory_budget() const noexcept
{
    DeviceLocalMemoryBudget result{};
    if (!m_hasMemoryBudgetExtension || m_instance == VK_NULL_HANDLE || m_physicalDevice == VK_NULL_HANDLE)
        return result;

    const auto now = std::chrono::steady_clock::now();
    if (m_hasCachedMemoryBudget && (now - m_lastMemoryBudgetQuery) < MemoryBudgetCacheInterval)
        return m_cachedMemoryBudget;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps{};
    budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 memoryProps{};
    memoryProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memoryProps.pNext = &budgetProps;

    if (m_getPhysicalDeviceMemoryProperties2 != nullptr)
    {
        m_getPhysicalDeviceMemoryProperties2(m_physicalDevice, &memoryProps);
    }
    else
    {
        if (m_getPhysicalDeviceMemoryProperties2KHR == nullptr)
            return result;
        m_getPhysicalDeviceMemoryProperties2KHR(
            m_physicalDevice, reinterpret_cast<VkPhysicalDeviceMemoryProperties2KHR*>(&memoryProps));
    }

    for (uint32_t i = 0; i < memoryProps.memoryProperties.memoryHeapCount; ++i)
    {
        if ((memoryProps.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0)
            continue;
        result.usageBytes += budgetProps.heapUsage[i];
        result.budgetBytes += budgetProps.heapBudget[i];
    }

    result.supported = result.budgetBytes > 0;
    m_cachedMemoryBudget = result;
    m_lastMemoryBudgetQuery = now;
    m_hasCachedMemoryBudget = true;
    return m_cachedMemoryBudget;
}

Result<> VulkanDevice::create_logical_device()
{
    QueueFamilyIndices indices = find_queue_families(m_physicalDevice);

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

    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
    {
        return make_error("Failed to create logical device", ErrorCode::VulkanLogicalDeviceCreationFailed);
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);

    return {};
}

Result<> VulkanDevice::create_command_pool()
{
    QueueFamilyIndices queueFamilyIndices = find_queue_families(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
    {
        return make_error("Failed to create command pool", ErrorCode::VulkanCommandPoolCreationFailed);
    }

    return {};
}

// --- Queue Family / Swap Chain queries ---

QueueFamilyIndices VulkanDevice::find_queue_families(VkPhysicalDevice device) noexcept
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
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.is_complete())
            break;

        i++;
    }

    return indices;
}

SwapChainSupportDetails VulkanDevice::query_swap_chain_support(VkPhysicalDevice device) noexcept
{
    SwapChainSupportDetails details{};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

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

// --- Buffer / Image utilities ---

Result<uint32_t> VulkanDevice::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

Result<> VulkanDevice::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                     VkBuffer& buffer, VkDeviceMemory& bufferMemory,
                                     VkDeviceSize* outAllocationBytes)
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

    if (outAllocationBytes != nullptr)
        *outAllocationBytes = allocInfo.allocationSize;

    return {};
}

Result<VkCommandBuffer> VulkanDevice::begin_single_time_commands()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        return make_error("Failed to allocate one-time command buffer", ErrorCode::VulkanCommandBufferAllocationFailed);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to begin one-time command buffer", ErrorCode::VulkanCommandBufferRecordingFailed);
    }

    return commandBuffer;
}

Result<> VulkanDevice::end_single_time_commands(VkCommandBuffer commandBuffer)
{
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to end one-time command buffer", ErrorCode::VulkanCommandBufferRecordingFailed);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence transferFence{VK_NULL_HANDLE};
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &transferFence) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to create fence for one-time submit", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, transferFence) != VK_SUCCESS)
    {
        vkDestroyFence(m_device, transferFence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to submit one-time command buffer", ErrorCode::VulkanDrawFrameFailed);
    }

    if (vkWaitForFences(m_device, 1, &transferFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        vkDestroyFence(m_device, transferFence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Failed to wait for one-time command buffer fence", ErrorCode::VulkanDrawFrameFailed);
    }

    vkDestroyFence(m_device, transferFence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
    return {};
}

Result<> VulkanDevice::copy_buffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    auto commandBufferResult = begin_single_time_commands();
    if (!commandBufferResult)
        return make_error(commandBufferResult.error());
    VkCommandBuffer commandBuffer = commandBufferResult.value();

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    if (auto endResult = end_single_time_commands(commandBuffer); !endResult)
        return make_error("Failed to submit copy command", ErrorCode::VulkanCopyBufferFailed);
    return {};
}

Result<> VulkanDevice::create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                                    VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                                    VkDeviceMemory& imageMemory, VkSampleCountFlagBits samples,
                                    VkDeviceSize* outAllocationBytes)
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
    imageInfo.samples = samples;
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

    if (outAllocationBytes != nullptr)
        *outAllocationBytes = allocInfo.allocationSize;

    return {};
}

Result<VkImageView> VulkanDevice::create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
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

Result<> VulkanDevice::transition_image_layout(VkImage image, VkFormat format, VkImageLayout oldLayout,
                                               VkImageLayout newLayout)
{
    auto commandBufferResult = begin_single_time_commands();
    if (!commandBufferResult)
        return make_error("Failed to begin command buffer for layout transition",
                          ErrorCode::VulkanImageLayoutTransitionFailed);
    VkCommandBuffer commandBuffer = commandBufferResult.value();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage{};
    VkPipelineStageFlags destinationStage{};

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        return make_error("Unsupported layout transition", ErrorCode::VulkanImageLayoutTransitionFailed);
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (auto endResult = end_single_time_commands(commandBuffer); !endResult)
        return make_error("Failed to submit layout transition command", ErrorCode::VulkanImageLayoutTransitionFailed);
    return {};
}

Result<> VulkanDevice::copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    auto commandBufferResult = begin_single_time_commands();
    if (!commandBufferResult)
        return make_error("Failed to begin command buffer for buffer-to-image copy",
                          ErrorCode::VulkanCopyBufferToImageFailed);
    VkCommandBuffer commandBuffer = commandBufferResult.value();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (auto endResult = end_single_time_commands(commandBuffer); !endResult)
        return make_error("Failed to submit buffer-to-image copy command", ErrorCode::VulkanCopyBufferToImageFailed);
    return {};
}
