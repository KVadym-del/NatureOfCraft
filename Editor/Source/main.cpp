#include "UI/ImGuiLayer.hpp"
#include <Assets/Public/AssetManager.hpp>
#include <Camera/Public/Camera.hpp>
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Scene/Public/Scene.hpp>
#include <Scene/Public/SceneNode.hpp>
#include <Window/Public/Window.hpp>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fmt/core.h>
#include <string>

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

static const char* present_mode_name(VkPresentModeKHR mode)
{
    switch (mode)
    {
    case VK_PRESENT_MODE_FIFO_KHR:
        return "VSync On";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "Triple Buffered";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "VSync Off";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "VSync Relaxed";
    default:
        return "Unknown";
    }
}

static const char* vk_format_name(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    default:
        return "Other";
    }
}

static const char* msaa_sample_count_name(int samples)
{
    switch (samples)
    {
    case 1:
        return "1x (Off)";
    case 2:
        return "2x";
    case 4:
        return "4x";
    case 8:
        return "8x";
    case 16:
        return "16x";
    case 32:
        return "32x";
    case 64:
        return "64x";
    default:
        return "Unknown";
    }
}

static void draw_scene_hierarchy(SceneNode* node, SceneNode*& selectedNode)
{
    if (!node)
        return;

    const auto& children = node->get_children();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (node == selectedNode)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = node->get_name().empty() ? "(unnamed)" : node->get_name();
    if (node->has_mesh())
        label += fmt::format("  [mesh:{}]", node->get_mesh_index());

    bool open = false;
    if (children.empty())
    {
        ImGui::TreeNodeEx(node, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selectedNode = node;
    }
    else
    {
        open = ImGui::TreeNodeEx(node, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selectedNode = node;
        if (open)
        {
            for (const auto& child : children)
                draw_scene_hierarchy(child.get(), selectedNode);
            ImGui::TreePop();
        }
    }
}

static constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

static void draw_transform_inspector(SceneNode* node)
{
    if (!node)
        return;

    Transform& t = node->get_transform();

    // --- Position ---
    float pos[3] = {t.position.x, t.position.y, t.position.z};
    if (ImGui::DragFloat3("Position", pos, 0.05f))
        t.position = {pos[0], pos[1], pos[2]};

    // --- Rotation (displayed as degrees, stored as quaternion) ---
    DirectX::XMFLOAT3 euler = t.get_rotation_euler();
    float rot[3] = {euler.x * RAD_TO_DEG, euler.y * RAD_TO_DEG, euler.z * RAD_TO_DEG};
    if (ImGui::DragFloat3("Rotation", rot, 0.5f, -360.0f, 360.0f))
        t.set_rotation_euler(rot[0] * DEG_TO_RAD, rot[1] * DEG_TO_RAD, rot[2] * DEG_TO_RAD);

    // --- Scale ---
    float scl[3] = {t.scale.x, t.scale.y, t.scale.z};
    if (ImGui::DragFloat3("Scale", scl, 0.01f, 0.001f, 100.0f))
        t.scale = {scl[0], scl[1], scl[2]};
}

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

    // Load model (OBJ + MTL, splits by material)
    auto modelHandle = assetManager.load_model("Resources/wooden_watch_tower2.obj");
    const ModelData& model = *modelHandle;

    // Upload textures and materials, then meshes
    // For each material: load its textures (or use defaults) and upload a GPU material
    std::vector<uint32_t> gpuMaterialIndices;
    gpuMaterialIndices.reserve(model.materials.size());

    for (const auto& mat : model.materials)
    {
        // Albedo texture
        uint32_t albedoTexIdx = 0; // will be overwritten or left as default
        if (!mat.albedoTexturePath.empty())
        {
            auto texHandle = assetManager.load_texture(mat.albedoTexturePath);
            auto texResult = renderer.upload_texture(*texHandle);
            if (texResult)
                albedoTexIdx = texResult.value();
            else
                fmt::print("Warning: failed to upload albedo texture '{}': {}\n", mat.albedoTexturePath,
                           texResult.error().message);
        }

        // Normal texture
        uint32_t normalTexIdx = 0; // will be overwritten or left as default
        if (!mat.normalTexturePath.empty())
        {
            auto texHandle = assetManager.load_texture(mat.normalTexturePath);
            auto texResult = renderer.upload_texture(*texHandle);
            if (texResult)
                normalTexIdx = texResult.value();
            else
                fmt::print("Warning: failed to upload normal texture '{}': {}\n", mat.normalTexturePath,
                           texResult.error().message);
        }

        // Create GPU material
        auto matResult = renderer.upload_material(albedoTexIdx, normalTexIdx);
        if (matResult)
        {
            gpuMaterialIndices.push_back(matResult.value());
        }
        else
        {
            fmt::print("Warning: failed to upload material '{}': {}\n", mat.name, matResult.error().message);
            gpuMaterialIndices.push_back(0); // fallback to default material
        }
    }

    // Upload meshes and create scene nodes
    Scene scene;
    SceneNode* modelRoot = scene.get_root()->add_child("wooden_watch_tower2");

    for (size_t i = 0; i < model.meshes.size(); ++i)
    {
        auto uploadResult = renderer.upload_mesh(model.meshes[i]);
        if (!uploadResult)
        {
            fmt::print("Failed to upload mesh '{}': {}\n", model.meshes[i].name, uploadResult.error().message);
            continue;
        }
        uint32_t meshIdx = uploadResult.value();

        std::string nodeName = model.meshes[i].name.empty() ? fmt::format("SubMesh_{}", i) : model.meshes[i].name;
        SceneNode* meshNode = modelRoot->add_child(std::move(nodeName));
        meshNode->set_mesh_index(static_cast<int32_t>(meshIdx));

        // Assign material
        int32_t matMapIdx = model.meshMaterialIndices[i];
        if (matMapIdx >= 0 && static_cast<size_t>(matMapIdx) < gpuMaterialIndices.size())
        {
            meshNode->set_material_index(static_cast<int32_t>(gpuMaterialIndices[matMapIdx]));
        }
    }

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

    // Frame timing for stats overlay
    float deltaTime = 0.0f;
    float fpsSmoothed = 0.0f;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();
    const std::string gpuName = vulkan.get_gpu_name();

    // Currently selected node in the scene hierarchy
    SceneNode* selectedNode = nullptr;

    if (auto code = get_error_code(window.loop([&]() {
            // Frame timing
            auto now = std::chrono::high_resolution_clock::now();
            deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            // Exponential moving average for smooth FPS display
            float instantFps = (deltaTime > 0.0f) ? 1.0f / deltaTime : 0.0f;
            fpsSmoothed = fpsSmoothed * 0.95f + instantFps * 0.05f;

            // Update camera matrices each frame
            uint32_t rw = renderer.get_render_width();
            uint32_t rh = renderer.get_render_height();
            float ar = (rh > 0) ? static_cast<float>(rw) / static_cast<float>(rh) : 1.0f;
            renderer.set_view_projection(camera.get_view_matrix(), camera.get_projection_matrix(ar));

            // Update scene: turntable rotation on the model root node
            auto currentTime = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
            DirectX::XMFLOAT3 yAxis{0.0f, 1.0f, 0.0f};
            modelRoot->get_transform().set_rotation_axis(yAxis, elapsed * 0.5f);

            // Collect renderables from the scene and pass to the renderer
            renderer.set_renderables(scene.collect_renderables());

            Editor::UI::NewFrame();

            // --- Renderer Stats Overlay ---
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.6f);
            if (ImGui::Begin("Renderer Stats", nullptr,
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("FPS: %.1f", fpsSmoothed);
                ImGui::Text("Frame Time: %.2f ms", deltaTime * 1000.0f);
                ImGui::Separator();
                ImGui::Text("GPU: %s", gpuName.c_str());
                ImGui::Text("Resolution: %u x %u", rw, rh);
                ImGui::Text("Present Mode: %s", present_mode_name(vulkan.get_present_mode()));
                ImGui::Text("Surface Format: %s", vk_format_name(vulkan.get_swapchain_format()));
                ImGui::Text("MSAA: %s", msaa_sample_count_name(renderer.get_msaa_samples()));
                ImGui::Text("Render Scale: %.0f%%", renderer.get_render_scale() * 100.0f);
                ImGui::Text("Swapchain Images: %u", vulkan.get_swapchain_image_count());
                ImGui::Separator();
                ImGui::Text("Draw Calls: %u", vulkan.get_renderable_count());
                ImGui::Text("Triangles: %u", vulkan.get_total_triangle_count());
                ImGui::Text("Meshes Loaded: %u", vulkan.get_mesh_count());
                ImGui::Text("Textures Loaded: %u", vulkan.get_texture_count());
                ImGui::Text("Materials Loaded: %u", vulkan.get_material_count());
            }
            ImGui::End();

            // --- Settings Panel ---
            ImGui::SetNextWindowPos(ImVec2(10.0f, 350.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                // MSAA dropdown
                int currentMsaa = renderer.get_msaa_samples();
                const char* msaaLabel = msaa_sample_count_name(currentMsaa);
                if (ImGui::BeginCombo("MSAA", msaaLabel))
                {
                    constexpr int msaaOptions[] = {1, 2, 4, 8};
                    for (int opt : msaaOptions)
                    {
                        bool isSelected = (currentMsaa == opt);
                        if (ImGui::Selectable(msaa_sample_count_name(opt), isSelected))
                        {
                            if (opt != currentMsaa)
                                renderer.set_msaa_samples(opt);
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // Render Scale slider
                float renderScale = renderer.get_render_scale();
                if (ImGui::SliderFloat("Render Scale", &renderScale, 0.25f, 2.0f, "%.2f"))
                    renderer.set_render_scale(renderScale);

                // Present Mode selection
                KHR_Settings currentSetting = renderer.get_vsync()
                    ? KHR_Settings::VSync
                    : (vulkan.get_present_mode() == VK_PRESENT_MODE_MAILBOX_KHR
                        ? KHR_Settings::Triple_Buffering
                        : KHR_Settings::Immediate);

                int selected = static_cast<int>(currentSetting);
                ImGui::Text("Present Mode");
                if (ImGui::RadioButton("VSync", &selected, static_cast<int>(KHR_Settings::VSync)))
                    renderer.set_vsync(KHR_Settings::VSync);
                ImGui::SameLine();
                if (ImGui::RadioButton("Triple Buffered", &selected, static_cast<int>(KHR_Settings::Triple_Buffering)))
                    renderer.set_vsync(KHR_Settings::Triple_Buffering);
                ImGui::SameLine();
                if (ImGui::RadioButton("Immediate", &selected, static_cast<int>(KHR_Settings::Immediate)))
                    renderer.set_vsync(KHR_Settings::Immediate);
            }
            ImGui::End();

            // --- Add Object ---
            ImGui::SetNextWindowPos(ImVec2(10.0f, 500.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Objects", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (ImGui::Button("Add Object"))
                {
                    static int addedObjectCount = 0;
                    ++addedObjectCount;
                    std::string name = fmt::format("Object_{}", addedObjectCount);
                    SceneNode* newNode = modelRoot->add_child(std::move(name));

                    // Random-ish position based on object count
                    float angle = static_cast<float>(addedObjectCount) * 1.2f;
                    float radius = 3.0f + static_cast<float>(addedObjectCount) * 0.5f;
                    float x = radius * std::cos(angle);
                    float z = radius * std::sin(angle);
                    newNode->get_transform().position = {x, 0.0f, z};

                    // Reuse first mesh if available
                    if (vulkan.get_mesh_count() > 0)
                    {
                        newNode->set_mesh_index(0);
                        newNode->set_material_index(0);
                    }
                }
                ImGui::SameLine();
                ImGui::Text("(%u children under model root)", static_cast<uint32_t>(modelRoot->get_children().size()));
            }
            ImGui::End();

            // --- Scene Hierarchy ---
            ImGui::SetNextWindowPos(ImVec2(600.0f, 10.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 400.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene Hierarchy"))
            {
                draw_scene_hierarchy(scene.get_root(), selectedNode);
            }
            ImGui::End();

            // --- Transform Inspector ---
            ImGui::SetNextWindowPos(ImVec2(600.0f, 420.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (selectedNode)
                {
                    ImGui::Text("Node: %s", selectedNode->get_name().c_str());
                    if (selectedNode->has_mesh())
                        ImGui::Text("Mesh Index: %d", selectedNode->get_mesh_index());
                    if (selectedNode->get_material_index() >= 0)
                        ImGui::Text("Material Index: %d", selectedNode->get_material_index());
                    ImGui::Separator();
                    draw_transform_inspector(selectedNode);

                    ImGui::Separator();
                    if (ImGui::Button("Deselect"))
                        selectedNode = nullptr;
                }
                else
                {
                    ImGui::TextDisabled("Select a node in the Scene Hierarchy");
                }
            }
            ImGui::End();

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
