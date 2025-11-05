#include "UI/ImGuiLayer.hpp"
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Window/Public/Window.hpp>
#include <imgui.h>

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

int main()
{
    Window window{WIDTH, HEIGHT, "NatureOfCraft"};

    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    Vulkan vulkan{window.get_glfw_window()};

    if (auto code = get_error_code(vulkan.initialize()); code != 0)
        return code;

    if (!Editor::UI::InitializeImGuiForVulkan(vulkan, window.get_glfw_window()))
        return -1;

    if (auto code = get_error_code(window.loop([&vulkan]() {
            Editor::UI::NewFrame();
            ImGui::Begin("Status");
            ImGui::TextUnformatted("ImGui is integrated with Vulkan backend (editor-side).");
            ImGui::End();
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
    vulkan.cleanup();

    return 0;
}
