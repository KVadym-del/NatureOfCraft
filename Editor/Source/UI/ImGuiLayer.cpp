#include "ImGuiLayer.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <Rendering/BackEnds/Public/Vulkan.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace
{
VkDescriptorPool g_ImGuiDescriptorPool{};
bool g_ImGuiInitialized{};

VkDescriptorPool create_imgui_descriptor_pool(VkDevice device)
{
    std::array<VkDescriptorPoolSize, 11> pool_sizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();

    VkDescriptorPool pool{};
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS)
        return nullptr;

    return pool;
}
} // namespace

namespace Editor::UI
{

bool InitializeImGuiForVulkan(Vulkan& vulkan, GLFWwindow* window)
{
    if (g_ImGuiInitialized)
        return true;

    if (ImGui::GetCurrentContext() == nullptr)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    ImGui_ImplGlfw_InitForVulkan(window, true);

    VkDevice device = vulkan.get_device();
    g_ImGuiDescriptorPool = create_imgui_descriptor_pool(device);
    if (g_ImGuiDescriptorPool == VK_NULL_HANDLE)
        return false;

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = vulkan.get_instance();
    init_info.PhysicalDevice = vulkan.get_physical_device();
    init_info.Device = device;
    init_info.QueueFamily = vulkan.get_graphics_queue_family_index();
    init_info.Queue = vulkan.get_graphics_queue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = g_ImGuiDescriptorPool;
    init_info.RenderPass = vulkan.get_render_pass();
    init_info.Subpass = 0;
    init_info.MinImageCount = std::max<uint32_t>(2u, vulkan.get_swapchain_image_count());
    init_info.ImageCount = vulkan.get_swapchain_image_count();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info))
        return false;

    vulkan.set_ui_render_callback(
        [](VkCommandBuffer cmd) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd); });

    g_ImGuiInitialized = true;
    return true;
}

void NewFrame()
{
    if (!g_ImGuiInitialized)
        return;

    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
}

void OnSwapchainRecreated(Vulkan& vulkan)
{
    if (!g_ImGuiInitialized)
        return;

    ImGui_ImplVulkan_SetMinImageCount(std::max<uint32_t>(2u, vulkan.get_swapchain_image_count()));
}

void Shutdown(Vulkan& vulkan)
{
    if (!g_ImGuiInitialized)
        return;

    vulkan.set_ui_render_callback(Vulkan::UIRenderCallback{});

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    if (g_ImGuiDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(vulkan.get_device(), g_ImGuiDescriptorPool, nullptr);
        g_ImGuiDescriptorPool = VK_NULL_HANDLE;
    }

    g_ImGuiInitialized = false;
}

} // namespace Editor::UI