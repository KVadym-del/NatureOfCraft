#pragma once

// Forward declarations to avoid heavy includes in the header.
class Vulkan;      // from Source/Rendering/BackEnds/Public/Vulkan.hpp
struct GLFWwindow; // from GLFW

namespace Editor::UI
{

// Initialize Dear ImGui for GLFW + Vulkan using engine-side Vulkan handles.
// - Creates/uses an ImGui context (does not destroy the context on Shutdown).
// - Initializes the platform backend (GLFW) and the renderer backend (Vulkan).
// - Creates a descriptor pool for ImGui usage and uploads font textures.
// - Installs a render callback into the engine Vulkan renderer so ImGui is
//   rendered inside the main render pass each frame.
// Returns true on success, false otherwise.
bool InitializeImGuiForVulkan(Vulkan& vulkan, GLFWwindow* window);

// Begins a new ImGui frame for the current frame.
// Must be called once per frame before building UI with ImGui:: functions.
void NewFrame();

// Notify the ImGui Vulkan backend that the swapchain was recreated.
// Should be called after the engine recreates swapchain resources.
void OnSwapchainRecreated(Vulkan& vulkan);

// Shuts down editor-side ImGui integrations for Vulkan + GLFW.
// - Removes the render callback from the engine Vulkan renderer.
// - Destroys ImGui Vulkan/GLFW backend objects and descriptor pool.
// Note: Does not destroy the ImGui context; the owner should do that if desired.
void Shutdown(Vulkan& vulkan);

} // namespace Editor::UI
