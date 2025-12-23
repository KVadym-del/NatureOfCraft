#include "UI/ImGuiLayer.hpp"
#include "Media/MediaPlayer.hpp"
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

    // Use unified MediaPlayer for both video and audio
    MediaPlayer mediaPlayer{};
    bool mediaLoaded = mediaPlayer.open("C:/Users/vadym/Downloads/gg32.mp4");
    
    VkDescriptorSet videoDescriptorSet = VK_NULL_HANDLE;
    if (mediaLoaded && mediaPlayer.has_video())
    {
        vulkan.init_video_texture(mediaPlayer.get_video_width(), mediaPlayer.get_video_height());
        videoDescriptorSet = ImGui_ImplVulkan_AddTexture(
            vulkan.get_video_sampler(),
            vulkan.get_video_image_view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // Start playback
    if (mediaLoaded)
    {
        mediaPlayer.play();
    }

    if (auto code = get_error_code(window.loop([&]() {
            // Update media player (decodes audio/video, keeps buffers filled)
            if (mediaLoaded)
            {
                mediaPlayer.update();

                // Grab and display video frame (synchronized to audio)
                if (mediaPlayer.has_video())
                {
                    uint8_t* pixels = mediaPlayer.grab_video_frame();
                    if (pixels)
                    {
                        vulkan.update_video_texture(pixels);
                    }
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

            if (mediaLoaded && mediaPlayer.has_video())
            {
                ImGui::Image((ImTextureID)videoDescriptorSet, ImGui::GetContentRegionAvail());
            }
            else
            {
                ImGui::Text("Could not load media.");
            }

            // Control bar
            ImGui::SetCursorPos(ImVec2(20, ImGui::GetWindowHeight() - 70));
            
            if (ImGui::Button(mediaPlayer.is_playing() ? "Pause" : "Play"))
            {
                mediaPlayer.toggle_playback();
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop"))
            {
                mediaPlayer.stop();
            }

            // Playback time display
            ImGui::SameLine();
            double playbackTime = mediaPlayer.get_playback_clock();
            int minutes = static_cast<int>(playbackTime) / 60;
            int seconds = static_cast<int>(playbackTime) % 60;
            ImGui::Text("Time: %02d:%02d", minutes, seconds);

            // Status line
            ImGui::SetCursorPos(ImVec2(20, ImGui::GetWindowHeight() - 30));
            ImGui::Text("Status: %s", mediaPlayer.is_playing() ? "Playing" : "Paused");

            if (mediaPlayer.has_video())
            {
                ImGui::SameLine();
                ImGui::Text("| Video: %dx%d (queue: %zu)", 
                    mediaPlayer.get_video_width(), 
                    mediaPlayer.get_video_height(),
                    mediaPlayer.get_video_queue_size());
            }

            if (mediaPlayer.has_audio())
            {
                ImGui::SameLine();
                ImGui::Text("| Audio: %dHz (buf: %zu)",
                    mediaPlayer.get_audio_sample_rate(),
                    mediaPlayer.get_audio_buffer_size());
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