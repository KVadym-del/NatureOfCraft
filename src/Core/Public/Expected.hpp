#pragma once
#include "Core.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

#include <fmt/core.h>

enum class ErrorCode
{
    None = 0,

    // GLFW Errors
    GLFWInitializationFailed = 1,
    GLFWFailedToCreateWindow,

    // Utils Errors
    FileReadFailed = 100,

    // Vulkan Errors
    VulkanGLFWWindowIsNull = 200,
    VulkanInstanceCreationFailed,
    VulkanValidationLayersNotSupported,
    VulkanDebugMessengerCreationFailed,
    VulkanPhysicalDeviceSelectionFailed,
    VulkanLogicalDeviceCreationFailed,
    VulkanSurfaceCreationFailed,
    VulkanSwapChainCreationFailed,
    VulkanImageViewCreationFailed,
    VulkanShaderModuleCreationFailed,
    VulkanGraphicsPipelineLayoutCreationFailed,
    VulkanRenderPassCreationFailed,
    VulkanGraphicsPipelineCreationFailed,
    VulkanFramebufferCreationFailed,
    VulkanCommandPoolCreationFailed,
    VulkanCommandBufferAllocationFailed,
    VulkanCommandBufferRecordingFailed,
    VulkanSyncObjectsCreationFailed,
    VulkanDrawFrameFailed,
};

struct Error
{
    std::string_view message{};
    ErrorCode code{ErrorCode::None};
};

template <typename T = void> using Result = std::expected<T, Error>;

inline static constexpr auto make_error(std::string_view message, ErrorCode code = ErrorCode::None)
{
    return std::unexpected(Error{message, code});
}

inline static constexpr auto make_error(const Error& error)
{
    return std::unexpected(error);
}

inline static constexpr std::uint32_t get_error_code(const Result<>& result) noexcept
{
    if (result)
        return static_cast<std::uint32_t>(ErrorCode::None);
    const auto& err = result.error();
    const std::uint32_t code = static_cast<std::uint32_t>(err.code);
    fmt::print("ERROR ({}): {}\n", code, err.message);
    return code;
}
