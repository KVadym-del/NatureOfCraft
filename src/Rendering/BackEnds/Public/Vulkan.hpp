#pragma once
#include "../../../Core/Public/Expected.hpp"

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

class Vulkan
{
  public:
    inline Vulkan() = default;
    inline ~Vulkan() noexcept
    {
        this->cleanup();
    };

  public:
    Result<> initialize() noexcept
    {
        if (auto result = this->create_instance(); !result)
            return result;
        if (auto result = this->setup_debug_messenger(); !result)
            return result;

        return {};
    }

    Result<> create_instance();

    Result<> setup_debug_messenger();

    void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept;

    void cleanup() noexcept;

  private:
    bool check_validation_layer_support() const noexcept;

    std::vector<const char*> get_required_extensions();

    static VKAPI_ATTR uint32_t VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                         VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                         void* pUserData);

    VkResult ceate_debug_utils_messenger_ext(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDebugUtilsMessengerEXT* pDebugMessenger) noexcept;

    void destroy_debug_utils_messenger_ext(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                           const VkAllocationCallbacks* pAllocator) noexcept;

  private:
    VkInstance m_instance{};

    static constexpr std::array<const char*, 1> m_validationLayers{"VK_LAYER_KHRONOS_validation"};
#ifdef NDEBUG
    static constexpr bool m_enableValidationLayers{false};
#else
    static constexpr bool m_enableValidationLayers{true};
#endif
    VkDebugUtilsMessengerEXT m_debugMessenger{};
};
