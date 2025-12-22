#include "UI/ImGuiLayer.hpp"
#include "Media/VideoPlayer.hpp"
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Window/Public/Window.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

int main()
{
    Window window{WIDTH, HEIGHT, "Native C++ Media Player"};

    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    Vulkan vulkan{window.get_glfw_window()};

    if (auto code = get_error_code(vulkan.initialize()); code != 0)
        return code;

    if (!Editor::UI::InitializeImGuiForVulkan(vulkan, window.get_glfw_window()))
        return -1;

    VideoPlayer player{};
    bool videoLoaded = player.open("C:/Users/vadym/Downloads/gg32.mp4");
    VkDescriptorSet videoDescriptorSet = VK_NULL_HANDLE;
    if (videoLoaded)
    {
        vulkan.init_video_texture(player.get_width(), player.get_height());
        videoDescriptorSet = ImGui_ImplVulkan_AddTexture(
            vulkan.get_video_sampler(),
            vulkan.get_video_image_view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    if (auto code = get_error_code(window.loop([&]() {
            if (videoLoaded)
            {
                uint8_t* pixels = player.grab_next_frame();
                if (pixels)
                {
                    vulkan.update_video_texture(pixels);
                }
            }

            Editor::UI::NewFrame();

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

            ImGui::Begin(
                "VideoPlayer",
                nullptr,
                ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoMove
            );

            if (videoLoaded)
            {
                ImGui::Image((ImTextureID)videoDescriptorSet, ImGui::GetContentRegionAvail());
            }
            else
            {
                ImGui::Text("Could not load video.");
            }

            ImGui::SetCursorPos(ImVec2(20, ImGui::GetWindowHeight() - 50));
            if (ImGui::Button("Play/Pause"))
            {
            }

            ImGui::End();
            ImGui::PopStyleVar();

            ImGui::Render();
            return vulkan.draw_frame();
        }));
        code != 0)
    {
        Editor::UI::Shutdown(vulkan);
        ImGui::DestroyContext();
        return code;
    }

    vulkan.wait_idle();
    Editor::UI::Shutdown(vulkan);
    ImGui::DestroyContext();

    return 0;
}