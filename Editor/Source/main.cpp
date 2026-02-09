#include "UI/ImGuiLayer.hpp"
#include <Assets/Public/AssetManager.hpp>
#include <Camera/Public/Camera.hpp>
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Scene/Public/Scene.hpp>
#include <Window/Public/Window.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <fmt/core.h>

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

int main()
{
    Window window{WIDTH, HEIGHT, "NatureOfCraft"};

    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    Vulkan vulkan{window.get_glfw_window()};
    IRenderer& renderer = vulkan;

    // Wire framebuffer resize from Window to renderer
    window.set_framebuffer_size_callback([&renderer](int, int) { renderer.on_framebuffer_resized(); });

    if (auto code = get_error_code(renderer.initialize()); code != 0)
        return code;

    // ImGui setup requires Vulkan-specific accessors (backend-specific)
    if (!Editor::UI::InitializeImGuiForVulkan(vulkan, window.get_glfw_window()))
        return -1;

    // --- Scene setup: load model via AssetManager and upload to GPU ---
    AssetManager assetManager;
    auto meshHandle = assetManager.load_mesh("Resources/Mustang.obj");
    auto uploadResult = renderer.upload_mesh(*meshHandle);
    if (!uploadResult)
    {
        fmt::print("Failed to upload mesh: {}\n", uploadResult.error().message);
        return static_cast<int>(uploadResult.error().code);
    }
    uint32_t mustangMeshIndex = uploadResult.value();

    Scene scene;
    SceneNode* mustangNode = scene.get_root()->add_child("Mustang");
    mustangNode->set_mesh_index(static_cast<int32_t>(mustangMeshIndex));

    auto startTime = std::chrono::high_resolution_clock::now();

    // Camera setup: target at model center, distance 10, slight elevation
    OrbitCamera camera({0.0f, 1.0f, 0.0f}, 10.0f, 0.0f, 15.0f);

    // Input state for mouse-driven camera control
    bool rightMouseDown = false;
    bool middleMouseDown = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool firstMouse = true;

    window.set_mouse_button_callback([&](int button, int action, int /*mods*/) {
        // Let ImGui handle input when it wants focus
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            rightMouseDown = (action == GLFW_PRESS);
            if (action == GLFW_PRESS)
                firstMouse = true;
        }
        if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            middleMouseDown = (action == GLFW_PRESS);
            if (action == GLFW_PRESS)
                firstMouse = true;
        }
    });

    window.set_cursor_pos_callback([&](double xpos, double ypos) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        if (firstMouse)
        {
            lastMouseX = xpos;
            lastMouseY = ypos;
            firstMouse = false;
            return;
        }

        double dx = xpos - lastMouseX;
        double dy = ypos - lastMouseY;
        lastMouseX = xpos;
        lastMouseY = ypos;

        if (rightMouseDown)
        {
            camera.rotate(static_cast<float>(-dx), static_cast<float>(dy));
        }
        if (middleMouseDown)
        {
            camera.pan(static_cast<float>(dx), static_cast<float>(dy));
        }
    });

    window.set_scroll_callback([&](double /*xoffset*/, double yoffset) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;

        camera.zoom(static_cast<float>(yoffset));
    });

    // Set initial view/projection
    float aspectRatio = static_cast<float>(WIDTH) / static_cast<float>(HEIGHT);
    renderer.set_view_projection(camera.get_view_matrix(), camera.get_projection_matrix(aspectRatio));

    if (auto code = get_error_code(window.loop([&]() {
            // Update camera matrices each frame
            uint32_t rw = renderer.get_render_width();
            uint32_t rh = renderer.get_render_height();
            float ar = (rh > 0) ? static_cast<float>(rw) / static_cast<float>(rh) : 1.0f;
            renderer.set_view_projection(camera.get_view_matrix(), camera.get_projection_matrix(ar));

            // Update scene: turntable rotation on the Mustang node
            auto currentTime = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
            DirectX::XMFLOAT3 yAxis{0.0f, 1.0f, 0.0f};
            mustangNode->get_transform().set_rotation_axis(yAxis, elapsed * 0.5f);

            // Collect renderables from the scene and pass to the renderer
            renderer.set_renderables(scene.collect_renderables());

            Editor::UI::NewFrame();

            ImGui::Render();
            return renderer.draw_frame();
        }));
        code != 0)
    {
        Editor::UI::Shutdown(vulkan);
        ImGui::DestroyContext();
        return code;
    }

    renderer.wait_idle();
    Editor::UI::Shutdown(vulkan);
    ImGui::DestroyContext();

    return 0;
}
