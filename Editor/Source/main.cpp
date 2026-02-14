#define NOMINMAX
#include "UI/ImGuiLayer.hpp"
#include <Assets/Private/ModelLoader.hpp>
#include <Assets/Public/AssetManager.hpp>
#include <Assets/Public/ModelData.hpp>
#include <Camera/Public/Camera.hpp>
#include <ECS/Public/Components.hpp>
#include <Level/Public/Level.hpp>
#include <Level/Public/Project.hpp>
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Scripting/Public/ScriptEngine.hpp>
#include <Window/Public/Window.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>

#include <entt/entity/registry.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ── Constants ─────────────────────────────────────────────────────────

constexpr static std::uint32_t WIDTH{800};
constexpr static std::uint32_t HEIGHT{600};

// ── Editor state machine ──────────────────────────────────────────────

enum class EditorState
{
    ProjectBrowser,
    LevelEditor,
};

// ── Status message ────────────────────────────────────────────────────

struct StatusMessage
{
    std::string text;
    bool isError{false};
    float timer{0.0f}; // seconds remaining
};

// ── Helper: format name utilities ─────────────────────────────────────

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

static KHR_Settings present_mode_to_setting(VkPresentModeKHR mode)
{
    switch (mode)
    {
    case VK_PRESENT_MODE_FIFO_KHR:
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return KHR_Settings::VSync;
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return KHR_Settings::Triple_Buffering;
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return KHR_Settings::Immediate;
    default:
        return KHR_Settings::VSync;
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

static float bytes_to_mib(uint64_t bytes)
{
    return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

enum class ViewportAspectMode
{
    Free,
    Aspect16x9,
    Aspect4x3,
    Aspect1x1,
    Aspect21x9,
};

static const char* viewport_aspect_mode_name(ViewportAspectMode mode)
{
    switch (mode)
    {
    case ViewportAspectMode::Free:
        return "AR: Free";
    case ViewportAspectMode::Aspect16x9:
        return "AR: 16:9";
    case ViewportAspectMode::Aspect4x3:
        return "AR: 4:3";
    case ViewportAspectMode::Aspect1x1:
        return "AR: 1:1";
    case ViewportAspectMode::Aspect21x9:
        return "AR: 21:9";
    default:
        return "AR: Free";
    }
}

static float viewport_aspect_mode_value(ViewportAspectMode mode)
{
    switch (mode)
    {
    case ViewportAspectMode::Aspect16x9:
        return 16.0f / 9.0f;
    case ViewportAspectMode::Aspect4x3:
        return 4.0f / 3.0f;
    case ViewportAspectMode::Aspect1x1:
        return 1.0f;
    case ViewportAspectMode::Aspect21x9:
        return 21.0f / 9.0f;
    case ViewportAspectMode::Free:
    default:
        return 0.0f;
    }
}

// ── Entity-based scene hierarchy drawer ───────────────────────────────

static void draw_entity_hierarchy(World& world, entt::entity entity, entt::entity& selectedEntity)
{
    auto& reg = world.registry();
    if (!reg.valid(entity))
        return;

    const auto& name = reg.get<NameComponent>(entity);
    const auto& hierarchy = reg.get<HierarchyComponent>(entity);
    const auto* mesh = reg.try_get<MeshComponent>(entity);

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (hierarchy.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (entity == selectedEntity)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = name.name.empty() ? "(unnamed)" : name.name;
    if (mesh && mesh->meshIndex >= 0)
        label += fmt::format("  [mesh:{}]", mesh->meshIndex);

    void* nodeId = reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(entity)));

    bool open = false;
    if (hierarchy.children.empty())
    {
        ImGui::TreeNodeEx(nodeId, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selectedEntity = entity;
    }
    else
    {
        open = ImGui::TreeNodeEx(nodeId, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            selectedEntity = entity;
        if (open)
        {
            for (entt::entity child : hierarchy.children)
                draw_entity_hierarchy(world, child, selectedEntity);
            ImGui::TreePop();
        }
    }
}

// ── Entity-based transform inspector ──────────────────────────────────

static constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
static constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

static void draw_transform_inspector(TransformComponent& t)
{
    float pos[3] = {t.position.x, t.position.y, t.position.z};
    if (ImGui::DragFloat3("Position", pos, 0.05f))
        t.position = {pos[0], pos[1], pos[2]};

    DirectX::XMFLOAT3 euler = t.get_rotation_euler();
    float rot[3] = {euler.x * RAD_TO_DEG, euler.y * RAD_TO_DEG, euler.z * RAD_TO_DEG};
    if (ImGui::DragFloat3("Rotation", rot, 0.5f, -360.0f, 360.0f))
        t.set_rotation_euler(rot[0] * DEG_TO_RAD, rot[1] * DEG_TO_RAD, rot[2] * DEG_TO_RAD);

    float scl[3] = {t.scale.x, t.scale.y, t.scale.z};
    if (ImGui::DragFloat3("Scale", scl, 0.01f, 0.001f, 100.0f))
        t.scale = {scl[0], scl[1], scl[2]};
}

// ── Script scanner ────────────────────────────────────────────────────
// Scans for .lua files in the project directory.
// Returns project-relative paths (e.g. "Scripts/spin.lua") suitable for ScriptComponent.

static std::vector<std::string> scan_available_scripts(const Project* project)
{
    std::set<std::string> scripts;

    if (!project)
        return {};

    fs::path root(project->root_path());
    if (!fs::exists(root))
        return {};

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua")
            scripts.insert(fs::relative(entry.path(), root).generic_string());
    }

    return {scripts.begin(), scripts.end()};
}

struct ProjectAssetEntry
{
    std::string relativePath;
    std::string category;
    uintmax_t sizeBytes{0};
};

static std::string to_lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool extension_in(const fs::path& path, const std::vector<std::string>& exts)
{
    std::string ext = to_lower_copy(path.extension().string());
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

static std::vector<ProjectAssetEntry> scan_project_assets(const Project* project)
{
    if (!project)
        return {};

    const fs::path root(project->root_path());
    if (!fs::exists(root))
        return {};

    const std::vector<std::string> textureExts = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".ktx", ".ktx2"};
    const std::vector<std::string> shaderExts = {".vert", ".frag", ".comp", ".geom", ".tesc", ".tese"};
    const std::vector<std::string> scriptExts = {".lua"};

    std::vector<ProjectAssetEntry> assets;
    auto collect_from_dir = [&](const fs::path& relativeDir, const std::string& category,
                                const std::vector<std::string>& allowedExts) {
        std::error_code ec;
        fs::path absDir = root / relativeDir;
        if (!fs::exists(absDir, ec))
            return;

        for (const auto& entry :
             fs::recursive_directory_iterator(absDir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec || !entry.is_regular_file())
                continue;

            if (!allowedExts.empty() && !extension_in(entry.path(), allowedExts))
                continue;

            uintmax_t sizeBytes = entry.file_size(ec);
            if (ec)
                sizeBytes = 0;

            assets.push_back(ProjectAssetEntry{
                fs::relative(entry.path(), root, ec).generic_string(),
                category,
                sizeBytes,
            });
        }
    };

    collect_from_dir(fs::path("Assets") / "Models", "Optimized Models", {".noc_model"});
    collect_from_dir(fs::path("Assets") / "Textures", "Textures", textureExts);
    collect_from_dir(fs::path("Assets") / "Shaders", "Shaders", shaderExts);
    collect_from_dir(fs::path("Scripts"), "Scripts", scriptExts);

    std::sort(assets.begin(), assets.end(), [](const ProjectAssetEntry& a, const ProjectAssetEntry& b) {
        if (a.category == b.category)
            return a.relativePath < b.relativePath;
        return a.category < b.category;
    });

    return assets;
}

// ── Import model helper ───────────────────────────────────────────────
// Loads an OBJ via AssetManager, uploads textures/materials/meshes to GPU,
// creates entity hierarchy in World.
// If a project is provided, copies textures and writes a .noc_model cache
// to the project's Assets/ directory so the project is self-contained.

static std::string copy_texture_to_project(const std::string& texturePath, const Project& project)
{
    if (texturePath.empty())
        return {};

    fs::path srcPath(texturePath);
    if (!fs::exists(srcPath))
    {
        fmt::print("Warning: texture not found, skipping copy: {}\n", texturePath);
        return {};
    }

    // Destination: <project>/Assets/Textures/<filename>
    fs::path relPath = fs::path("Assets") / "Textures" / srcPath.filename();
    std::string absPath = project.get_absolute_path(relPath);
    fs::create_directories(fs::path(absPath).parent_path());

    std::error_code ec;
    fs::copy_file(srcPath, absPath, fs::copy_options::skip_existing, ec);
    if (ec)
        fmt::print("Warning: failed to copy texture '{}' to '{}': {}\n", texturePath, absPath, ec.message());

    return relPath.string();
}

static bool import_model(std::string_view objPath, AssetManager& assetManager, IRenderer& renderer, World& world,
                         StatusMessage& status, Project* project)
{
    // Load model from OBJ + MTL
    entt::resource<ModelData> modelHandle;
    try
    {
        modelHandle = assetManager.load_model(objPath);
    }
    catch (const std::exception& e)
    {
        status = {fmt::format("Failed to load model '{}': {}", objPath, e.what()), true, 5.0f};
        return false;
    }

    if (!modelHandle)
    {
        status = {fmt::format("Failed to load model '{}'", objPath), true, 5.0f};
        return false;
    }

    // Work on a mutable copy so we can rewrite texture paths for caching
    ModelData model = *modelHandle;

    // If we have a project, copy textures and rewrite paths to be project-relative
    std::string cachedAssetPath;
    if (project)
    {
        for (auto& mat : model.materials)
        {
            mat.albedoTexturePath = copy_texture_to_project(mat.albedoTexturePath, *project);
            mat.normalTexturePath = copy_texture_to_project(mat.normalTexturePath, *project);
            mat.roughnessTexturePath = copy_texture_to_project(mat.roughnessTexturePath, *project);
        }

        // Write .noc_model cache to project's Assets/Models/
        std::string modelName = model.name;
        if (modelName.empty())
            modelName = fs::path(objPath).stem().string();

        fs::path cacheRelPath = fs::path("Assets") / "Models" / (modelName + ".noc_model");
        std::string cacheAbsPath = project->get_absolute_path(cacheRelPath);
        fs::create_directories(fs::path(cacheAbsPath).parent_path());

        auto writeResult = ModelLoader::write_cache(model, cacheAbsPath);
        if (writeResult)
        {
            cachedAssetPath = cacheRelPath.string();
            fmt::print("Wrote model cache: {}\n", cacheAbsPath);
        }
        else
        {
            fmt::print("Warning: failed to write model cache: {}\n", writeResult.error().message);
            // Fall back to storing the original OBJ path
            cachedAssetPath = std::string(objPath);
        }
    }
    else
    {
        // No project: store original path
        cachedAssetPath = std::string(objPath);
    }

    // Upload textures and materials to GPU
    // Texture paths in 'model' are now project-relative if we have a project,
    // so resolve them to absolute paths for loading.
    std::vector<uint32_t> gpuMaterialIndices;
    gpuMaterialIndices.reserve(model.materials.size());

    for (const auto& mat : model.materials)
    {
        uint32_t albedoTexIdx = 0;
        if (!mat.albedoTexturePath.empty())
        {
            std::string texLoadPath =
                project ? project->get_absolute_path(mat.albedoTexturePath) : mat.albedoTexturePath;
            auto texHandle = assetManager.load_texture(texLoadPath);
            auto texResult = renderer.upload_texture(*texHandle);
            if (texResult)
                albedoTexIdx = texResult.value();
            else
                fmt::print("Warning: failed to upload albedo texture '{}': {}\n", texLoadPath,
                           texResult.error().message);
        }

        uint32_t normalTexIdx = 0;
        if (!mat.normalTexturePath.empty())
        {
            std::string texLoadPath =
                project ? project->get_absolute_path(mat.normalTexturePath) : mat.normalTexturePath;
            auto texHandle = assetManager.load_texture(texLoadPath);
            auto texResult = renderer.upload_texture(*texHandle);
            if (texResult)
                normalTexIdx = texResult.value();
            else
                fmt::print("Warning: failed to upload normal texture '{}': {}\n", texLoadPath,
                           texResult.error().message);
        }

        auto matResult = renderer.upload_material(albedoTexIdx, normalTexIdx);
        if (matResult)
            gpuMaterialIndices.push_back(matResult.value());
        else
        {
            fmt::print("Warning: failed to upload material '{}': {}\n", mat.name, matResult.error().message);
            gpuMaterialIndices.push_back(0);
        }
    }

    // Create entity hierarchy
    std::string modelName = model.name;
    if (modelName.empty())
        modelName = fs::path(objPath).stem().string();

    entt::entity modelRoot = world.create_entity(modelName);

    uint32_t meshesUploaded = 0;
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
        entt::entity meshEntity = world.create_entity(std::move(nodeName));
        world.set_parent(meshEntity, modelRoot);

        auto& mc = world.registry().emplace<MeshComponent>(meshEntity);
        mc.meshIndex = static_cast<int32_t>(meshIdx);
        mc.assetPath = cachedAssetPath;

        int32_t matMapIdx = model.meshMaterialIndices[i];
        if (matMapIdx >= 0 && static_cast<size_t>(matMapIdx) < gpuMaterialIndices.size())
            mc.materialIndex = static_cast<int32_t>(gpuMaterialIndices[matMapIdx]);

        ++meshesUploaded;
    }

    status = {
        fmt::format("Imported '{}': {} meshes, {} materials", modelName, meshesUploaded, gpuMaterialIndices.size()),
        false, 5.0f};

    return true;
}

// ── Reload level assets helper ────────────────────────────────────────
// When a level is loaded from disk, MeshComponent entities have stale GPU indices.
// This function re-imports all referenced models, uploads them to the GPU, and
// updates every MeshComponent with fresh indices by matching entity names to
// mesh names in the model.

struct MeshGpuInfo
{
    int32_t gpuMeshIndex{-1};
    int32_t gpuMaterialIndex{0};
};

static void reload_level_assets(AssetManager& assetManager, IRenderer& renderer, World& world, StatusMessage& status,
                                Project* project)
{
    auto& reg = world.registry();

    // 1. Collect unique asset paths from all MeshComponents
    std::set<std::string> uniquePaths;
    auto meshView = reg.view<MeshComponent>();
    for (auto entity : meshView)
    {
        const auto& mc = meshView.get<MeshComponent>(entity);
        if (!mc.assetPath.empty())
            uniquePaths.insert(mc.assetPath);
    }

    if (uniquePaths.empty())
        return;

    // 2. For each unique asset path, load model & upload GPU resources.
    //    Build mapping: assetPath -> (meshName -> MeshGpuInfo)
    std::unordered_map<std::string, std::unordered_map<std::string, MeshGpuInfo>> assetMeshMap;

    uint32_t totalMeshes = 0;
    uint32_t totalMaterials = 0;

    for (const auto& assetPath : uniquePaths)
    {
        // Resolve project-relative paths to absolute for loading
        std::string loadPath =
            (project && !fs::path(assetPath).is_absolute()) ? project->get_absolute_path(assetPath) : assetPath;

        entt::resource<ModelData> modelHandle;
        try
        {
            modelHandle = assetManager.load_model(loadPath);
        }
        catch (const std::exception& e)
        {
            fmt::print("Warning: failed to reload model '{}': {}\n", loadPath, e.what());
            continue;
        }

        if (!modelHandle)
        {
            fmt::print("Warning: failed to reload model '{}'\n", loadPath);
            continue;
        }

        const ModelData& model = *modelHandle;

        // Upload textures and materials
        std::vector<uint32_t> gpuMaterialIndices;
        gpuMaterialIndices.reserve(model.materials.size());

        for (const auto& mat : model.materials)
        {
            uint32_t albedoTexIdx = 0;
            if (!mat.albedoTexturePath.empty())
            {
                // Texture paths in .noc_model are project-relative; resolve them
                std::string texLoadPath = (project && !fs::path(mat.albedoTexturePath).is_absolute())
                                              ? project->get_absolute_path(mat.albedoTexturePath)
                                              : mat.albedoTexturePath;
                auto texHandle = assetManager.load_texture(texLoadPath);
                auto texResult = renderer.upload_texture(*texHandle);
                if (texResult)
                    albedoTexIdx = texResult.value();
            }

            uint32_t normalTexIdx = 0;
            if (!mat.normalTexturePath.empty())
            {
                std::string texLoadPath = (project && !fs::path(mat.normalTexturePath).is_absolute())
                                              ? project->get_absolute_path(mat.normalTexturePath)
                                              : mat.normalTexturePath;
                auto texHandle = assetManager.load_texture(texLoadPath);
                auto texResult = renderer.upload_texture(*texHandle);
                if (texResult)
                    normalTexIdx = texResult.value();
            }

            auto matResult = renderer.upload_material(albedoTexIdx, normalTexIdx);
            if (matResult)
                gpuMaterialIndices.push_back(matResult.value());
            else
                gpuMaterialIndices.push_back(0);
        }
        totalMaterials += static_cast<uint32_t>(gpuMaterialIndices.size());

        // Upload meshes and build name -> gpu info mapping
        auto& nameMap = assetMeshMap[assetPath];

        for (size_t i = 0; i < model.meshes.size(); ++i)
        {
            auto uploadResult = renderer.upload_mesh(model.meshes[i]);
            if (!uploadResult)
            {
                fmt::print("Warning: failed to upload mesh '{}': {}\n", model.meshes[i].name,
                           uploadResult.error().message);
                continue;
            }

            MeshGpuInfo info;
            info.gpuMeshIndex = static_cast<int32_t>(uploadResult.value());

            int32_t matMapIdx = model.meshMaterialIndices[i];
            if (matMapIdx >= 0 && static_cast<size_t>(matMapIdx) < gpuMaterialIndices.size())
                info.gpuMaterialIndex = static_cast<int32_t>(gpuMaterialIndices[matMapIdx]);

            // Key by mesh name (same name used when creating entities during import)
            std::string meshName = model.meshes[i].name.empty() ? fmt::format("SubMesh_{}", i) : model.meshes[i].name;
            nameMap[meshName] = info;

            ++totalMeshes;
        }
    }

    // 3. Update every MeshComponent entity with fresh GPU indices
    uint32_t updatedCount = 0;
    for (auto entity : meshView)
    {
        auto& mc = meshView.get<MeshComponent>(entity);
        if (mc.assetPath.empty())
            continue;

        auto assetIt = assetMeshMap.find(mc.assetPath);
        if (assetIt == assetMeshMap.end())
        {
            // Model failed to load; mark mesh as invalid
            mc.meshIndex = -1;
            continue;
        }

        // Match entity name to mesh name in the model
        const auto* nameComp = reg.try_get<NameComponent>(entity);
        if (!nameComp)
        {
            mc.meshIndex = -1;
            continue;
        }

        auto meshIt = assetIt->second.find(nameComp->name);
        if (meshIt != assetIt->second.end())
        {
            mc.meshIndex = meshIt->second.gpuMeshIndex;
            mc.materialIndex = meshIt->second.gpuMaterialIndex;
            ++updatedCount;
        }
        else
        {
            // Entity name doesn't match any mesh name — stale data
            fmt::print("Warning: entity '{}' has assetPath '{}' but no matching mesh name\n", nameComp->name,
                       mc.assetPath);
            mc.meshIndex = -1;
        }
    }

    status = {fmt::format("Reloaded assets: {} meshes, {} materials ({} entities updated)", totalMeshes, totalMaterials,
                          updatedCount),
              false, 5.0f};
}

// ── Status message display ────────────────────────────────────────────

static void draw_status_message(StatusMessage& status, float deltaTime)
{
    if (status.timer <= 0.0f)
        return;

    status.timer -= deltaTime;
    if (status.timer <= 0.0f)
    {
        status = {};
        return;
    }

    ImVec4 color = status.isError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", status.text.c_str());
    ImGui::PopStyleColor();
}

// ── Main ──────────────────────────────────────────────────────────────

int main()
{
    Window window{WIDTH, HEIGHT, "NatureOfCraft"};

    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    Vulkan vulkan{window.get_glfw_window()};
    IRenderer& renderer = vulkan;

    window.set_framebuffer_size_callback([&renderer](int, int) { renderer.on_framebuffer_resized(); });

    if (auto code = get_error_code(renderer.initialize()); code != 0)
        return code;

    if (!Editor::UI::InitializeImGuiForVulkan(vulkan, window.get_glfw_window()))
        return -1;

    // ── Shared state ──────────────────────────────────────────────────
    AssetManager assetManager;
    OrbitCameraController cameraController;
    ScriptEngine scriptEngine;

    if (auto initResult = scriptEngine.initialize(); !initResult)
    {
        fmt::print("Failed to initialize ScriptEngine: {}\n", initResult.error().message);
        return -1;
    }

    EditorState editorState = EditorState::ProjectBrowser;
    std::unique_ptr<Project> project;
    std::unique_ptr<Level> level;

    StatusMessage statusMessage;
    entt::entity selectedEntity = entt::null;
    static int addedEntityCount = 0;

    // Input buffers for ImGui text fields
    char projectNameBuf[256] = "MyProject";
    char projectPathBuf[512] = "";
    char openProjectPathBuf[512] = "";
    char newLevelNameBuf[256] = "NewLevel";
    char importModelPathBuf[512] = "Resources/wooden_watch_tower2.obj";

    // Docking/panel visibility
    bool showRendererStatsPanel = true;
    bool showViewportPanel = true;
    bool showGraphicsPanel = true;
    bool showLevelPanel = true;
    bool showObjectsPanel = true;
    bool showSceneHierarchyPanel = true;
    bool showInspectorPanel = true;
    bool showAssetManagerPanel = true;

    struct GraphicsSettingsDraft
    {
        int msaaSamples{1};
        float renderScale{1.0f};
        KHR_Settings presentMode{KHR_Settings::VSync};
        bool initialized{false};
        bool dirty{false};
        bool autoApply{true};
    } graphicsDraft;
    bool graphicsApplyRequested{false};

    std::vector<ProjectAssetEntry> projectAssetsCache;
    bool projectAssetsDirty{true};
    int selectedProjectAsset{-1};
    char assetFilterBuf[256] = "";
    bool requestDefaultDockLayout{true};

    VkDescriptorSet viewportDescriptorSet{VK_NULL_HANDLE};
    VkImageView viewportDescriptorImageView{VK_NULL_HANDLE};
    bool viewportHovered{false};
    ViewportAspectMode viewportAspectMode{ViewportAspectMode::Free};

    // ── State transition helpers ──────────────────────────────────────

    auto clear_runtime_scene_state = [&]() {
        renderer.wait_idle();
        renderer.set_renderables({});

        if (level)
            scriptEngine.on_world_destroyed(level->world());

        if (auto clearResult = renderer.clear_scene_content(); !clearResult)
        {
            statusMessage = {fmt::format("Warning: failed to clear renderer scene content: {}",
                                         clearResult.error().message),
                             true, 5.0f};
        }

        assetManager.clear_materials();
        assetManager.clear_textures();
        assetManager.clear_models();
        assetManager.clear_meshes();
    };

    auto refresh_viewport_texture = [&]() {
        const VkImageView sceneView = vulkan.get_scene_color_image_view();
        if (sceneView == viewportDescriptorImageView && viewportDescriptorSet != VK_NULL_HANDLE)
            return;

        if (viewportDescriptorSet != VK_NULL_HANDLE)
        {
            renderer.wait_idle();
            ImGui_ImplVulkan_RemoveTexture(viewportDescriptorSet);
            viewportDescriptorSet = VK_NULL_HANDLE;
        }

        viewportDescriptorImageView = sceneView;
        if (sceneView != VK_NULL_HANDLE)
        {
            viewportDescriptorSet =
                ImGui_ImplVulkan_AddTexture(vulkan.get_sampler(), sceneView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    };

    auto release_viewport_texture = [&]() {
        if (viewportDescriptorSet != VK_NULL_HANDLE)
        {
            renderer.wait_idle();
            ImGui_ImplVulkan_RemoveTexture(viewportDescriptorSet);
            viewportDescriptorSet = VK_NULL_HANDLE;
        }
        viewportDescriptorImageView = VK_NULL_HANDLE;
    };

    auto sync_graphics_draft_from_runtime = [&]() {
        graphicsDraft.msaaSamples = renderer.get_msaa_samples();
        graphicsDraft.renderScale = renderer.get_render_scale();
        graphicsDraft.presentMode = present_mode_to_setting(vulkan.get_present_mode());
        graphicsDraft.initialized = true;
        graphicsDraft.dirty = false;
    };

    auto enter_level_editor = [&](std::unique_ptr<Level> newLevel) {
        clear_runtime_scene_state();
        level = std::move(newLevel);
        selectedEntity = entt::null;
        projectAssetsDirty = true;
        selectedProjectAsset = -1;
        showRendererStatsPanel = true;
        showViewportPanel = true;
        showGraphicsPanel = true;
        showLevelPanel = true;
        showObjectsPanel = true;
        showSceneHierarchyPanel = true;
        showInspectorPanel = true;
        showAssetManagerPanel = true;
        graphicsDraft.initialized = false;
        graphicsDraft.dirty = false;
        graphicsApplyRequested = false;
        requestDefaultDockLayout = true;

        World& world = level->world();

        // Configure script engine for this project's script directory
        if (project)
            scriptEngine.set_script_root(project->root_path());

        // Point the renderer at the project's shader sources (if available)
        if (project)
        {
            fs::path vertShader = project->get_absolute_path("Assets/Shaders/shader.vert");
            fs::path fragShader = project->get_absolute_path("Assets/Shaders/shader.frag");
            if (fs::exists(vertShader) && fs::exists(fragShader))
                renderer.set_shader_paths(vertShader, fragShader);
        }

        // Re-import all referenced models so entities have valid GPU indices
        renderer.wait_idle();
        reload_level_assets(assetManager, renderer, world, statusMessage, project.get());

        entt::entity camEntity = world.get_active_camera();
        if (camEntity != entt::null)
            cameraController.attach(world.registry(), camEntity);
        else
            cameraController.detach();

        editorState = EditorState::LevelEditor;
    };

    auto enter_project_browser = [&]() {
        cameraController.detach();
        clear_runtime_scene_state();
        level.reset();
        selectedEntity = entt::null;
        projectAssetsDirty = true;
        selectedProjectAsset = -1;
        showAssetManagerPanel = true;
        showViewportPanel = false;
        graphicsDraft.initialized = false;
        graphicsDraft.dirty = false;
        graphicsApplyRequested = false;
        requestDefaultDockLayout = true;
        editorState = EditorState::ProjectBrowser;
    };

    // Ensure ImGui viewport texture descriptors are rebuilt immediately
    // after swapchain/scene render target recreation.
    vulkan.set_swapchain_recreated_callback([&]() {
        Editor::UI::OnSwapchainRecreated(vulkan);
        release_viewport_texture();
        if (editorState == EditorState::LevelEditor && showViewportPanel)
            refresh_viewport_texture();
    });

    // ── Input state ───────────────────────────────────────────────────
    bool rightMouseDown = false;
    bool middleMouseDown = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool firstMouse = true;

    window.set_mouse_button_callback([&](int button, int action, int /*mods*/) {
        ImGuiIO& io = ImGui::GetIO();
        if (editorState != EditorState::LevelEditor)
            return;
        if (io.WantCaptureMouse && !viewportHovered)
            return;

        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            if (action == GLFW_PRESS && !viewportHovered)
                return;
            rightMouseDown = (action == GLFW_PRESS);
            if (action == GLFW_PRESS)
                firstMouse = true;
        }
        if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            if (action == GLFW_PRESS && !viewportHovered)
                return;
            middleMouseDown = (action == GLFW_PRESS);
            if (action == GLFW_PRESS)
                firstMouse = true;
        }
    });

    window.set_cursor_pos_callback([&](double xpos, double ypos) {
        ImGuiIO& io = ImGui::GetIO();
        if (editorState != EditorState::LevelEditor)
            return;
        if (io.WantCaptureMouse && !viewportHovered && !rightMouseDown && !middleMouseDown)
            return;
        if (!viewportHovered && !rightMouseDown && !middleMouseDown)
        {
            firstMouse = true;
            return;
        }

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
            cameraController.rotate(static_cast<float>(-dx), static_cast<float>(dy));
        if (middleMouseDown)
            cameraController.pan(static_cast<float>(dx), static_cast<float>(dy));
    });

    window.set_scroll_callback([&](double /*xoffset*/, double yoffset) {
        ImGuiIO& io = ImGui::GetIO();
        if (editorState != EditorState::LevelEditor)
            return;
        if (io.WantCaptureMouse && !viewportHovered)
            return;
        if (!viewportHovered)
            return;

        cameraController.zoom(static_cast<float>(yoffset));
    });

    // Frame timing
    float deltaTime = 0.0f;
    float fpsSmoothed = 0.0f;
    auto lastFrameTime = std::chrono::high_resolution_clock::now();
    const std::string gpuName = vulkan.get_gpu_name();

    // ── Main loop ─────────────────────────────────────────────────────

    if (auto code = get_error_code(window.loop([&]() {
            // Frame timing
            auto now = std::chrono::high_resolution_clock::now();
            deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;
            float instantFps = (deltaTime > 0.0f) ? 1.0f / deltaTime : 0.0f;
            fpsSmoothed = fpsSmoothed * 0.95f + instantFps * 0.05f;

            // Update camera and renderables if in level editor
            if (editorState == EditorState::LevelEditor && level)
            {
                uint32_t rw = renderer.get_render_width();
                uint32_t rh = renderer.get_render_height();
                float ar = (rh > 0) ? static_cast<float>(rw) / static_cast<float>(rh) : 1.0f;
                renderer.set_view_projection(cameraController.get_view_matrix(),
                                             cameraController.get_projection_matrix(ar));

                World& world = level->world();
                scriptEngine.update(world, deltaTime);
                world.update_world_matrices();
                renderer.set_renderables(world.collect_renderables());
            }
            else
            {
                // In project browser, render nothing
                renderer.set_renderables({});
            }

            // Apply graphics changes outside UI building to avoid descriptor/resource races.
            if (editorState == EditorState::LevelEditor && level)
            {
                if (!graphicsDraft.initialized)
                    sync_graphics_draft_from_runtime();

                if (graphicsApplyRequested)
                {
                    renderer.set_msaa_samples(graphicsDraft.msaaSamples);
                    renderer.set_render_scale(graphicsDraft.renderScale);
                    renderer.set_vsync(graphicsDraft.presentMode);
                    refresh_viewport_texture();
                    sync_graphics_draft_from_runtime();
                    if (!graphicsDraft.autoApply)
                        statusMessage = {"Graphics settings applied.", false, 2.5f};
                    graphicsApplyRequested = false;
                }
            }

            Editor::UI::NewFrame();
            viewportHovered = false;

            if (editorState == EditorState::LevelEditor)
                refresh_viewport_texture();

            // Dockspace host window (requires docking-enabled ImGui build)
            {
                ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                             ImGuiWindowFlags_NoBackground;

                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                ImGui::SetNextWindowViewport(viewport->ID);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::Begin("EditorDockspaceHost", nullptr, hostFlags);
                ImGui::PopStyleVar(3);

                ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");
                ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

                if (requestDefaultDockLayout)
                {
                    ImGui::DockBuilderRemoveNode(dockspaceId);
                    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
                    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

                    if (editorState == EditorState::ProjectBrowser)
                    {
                        ImGuiID dockMain = dockspaceId;
                        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.30f, nullptr, &dockMain);
                        ImGui::DockBuilderDockWindow("Project Browser", dockMain);
                        ImGui::DockBuilderDockWindow("Asset Manager", dockBottom);
                    }
                    else
                    {
                        ImGuiID dockMain = dockspaceId;
                        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.26f, nullptr, &dockMain);
                        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
                        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.24f, nullptr, &dockMain);

                        ImGuiID dockLeftTop = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Up, 0.44f, nullptr, &dockLeft);
                        ImGuiID dockLeftMid = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Up, 0.33f, nullptr, &dockLeft);
                        ImGuiID dockLeftMidTop = ImGui::DockBuilderSplitNode(dockLeftMid, ImGuiDir_Up, 0.55f, nullptr, &dockLeftMid);
                        ImGuiID dockRightTop = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.58f, nullptr, &dockRight);

                        ImGui::DockBuilderDockWindow("Viewport", dockMain);
                        ImGui::DockBuilderDockWindow("Renderer Stats", dockLeftTop);
                        ImGui::DockBuilderDockWindow("Graphics Settings", dockLeftMidTop);
                        ImGui::DockBuilderDockWindow("Objects", dockLeft);
                        ImGui::DockBuilderDockWindow("Level", dockLeftMid);
                        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockRightTop);
                        ImGui::DockBuilderDockWindow("Inspector", dockRight);
                        ImGui::DockBuilderDockWindow("Asset Manager", dockBottom);
                    }

                    ImGui::DockBuilderFinish(dockspaceId);
                    requestDefaultDockLayout = false;
                }

                if (ImGui::BeginMenuBar())
                {
                    if (ImGui::BeginMenu("View"))
                    {
                        ImGui::MenuItem("Viewport", nullptr, &showViewportPanel);
                        ImGui::MenuItem("Renderer Stats", nullptr, &showRendererStatsPanel);
                        ImGui::MenuItem("Graphics Settings", nullptr, &showGraphicsPanel);
                        ImGui::MenuItem("Objects", nullptr, &showObjectsPanel);
                        ImGui::MenuItem("Level", nullptr, &showLevelPanel);
                        ImGui::MenuItem("Scene Hierarchy", nullptr, &showSceneHierarchyPanel);
                        ImGui::MenuItem("Inspector", nullptr, &showInspectorPanel);
                        ImGui::MenuItem("Asset Manager", nullptr, &showAssetManagerPanel);
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Project"))
                    {
                        if (ImGui::MenuItem("Refresh Asset Cache", nullptr, false, project != nullptr))
                            projectAssetsDirty = true;
                        if (ImGui::MenuItem("Reset Default Layout"))
                            requestDefaultDockLayout = true;
                        ImGui::EndMenu();
                    }

                    ImGui::EndMenuBar();
                }

                ImGui::End();
            }

            // =============================================================
            // PROJECT BROWSER STATE
            // =============================================================
            if (editorState == EditorState::ProjectBrowser)
            {
                if (ImGui::Begin("Project Browser"))
                {
                    if (!project)
                    {
                        // ── No project open: create or open ──────────────
                        ImGui::SeparatorText("Create New Project");

                        ImGui::InputText("Project Name", projectNameBuf, sizeof(projectNameBuf));
                        ImGui::InputText("Directory", projectPathBuf, sizeof(projectPathBuf));
                        ImGui::SameLine();
                        ImGui::TextDisabled("(absolute path)");

                        if (ImGui::Button("Create Project"))
                        {
                            std::string pName{projectNameBuf};
                            std::string pPath{projectPathBuf};
                            if (pName.empty() || pPath.empty())
                            {
                                statusMessage = {"Project name and directory are required.", true, 4.0f};
                            }
                            else
                            {
                                auto result = Project::create_new(std::move(pName), std::move(pPath));
                                if (result)
                                {
                                    project = std::make_unique<Project>(std::move(result.value()));
                                    projectAssetsDirty = true;
                                    selectedProjectAsset = -1;
                                    requestDefaultDockLayout = true;
                                    auto saveResult = project->save();
                                    if (!saveResult)
                                        statusMessage = {fmt::format("Warning: could not save project manifest: {}",
                                                                     saveResult.error().message),
                                                         true, 5.0f};
                                    else
                                        statusMessage = {fmt::format("Created project '{}'", project->name()), false,
                                                         3.0f};
                                }
                                else
                                {
                                    statusMessage = {
                                        fmt::format("Failed to create project: {}", result.error().message), true,
                                        5.0f};
                                }
                            }
                        }

                        ImGui::Spacing();
                        ImGui::SeparatorText("Open Existing Project");

                        ImGui::InputText("Project File", openProjectPathBuf, sizeof(openProjectPathBuf));
                        ImGui::SameLine();
                        ImGui::TextDisabled("(.noc_project)");

                        if (ImGui::Button("Open Project"))
                        {
                            std::string fPath{openProjectPathBuf};
                            if (fPath.empty())
                            {
                                statusMessage = {"Please enter a .noc_project file path.", true, 4.0f};
                            }
                            else
                            {
                                auto result = Project::load(fPath);
                                if (result)
                                {
                                    project = std::make_unique<Project>(std::move(result.value()));
                                    projectAssetsDirty = true;
                                    selectedProjectAsset = -1;
                                    requestDefaultDockLayout = true;
                                    statusMessage = {fmt::format("Loaded project '{}'", project->name()), false, 3.0f};
                                }
                                else
                                {
                                    statusMessage = {fmt::format("Failed to load project: {}", result.error().message),
                                                     true, 5.0f};
                                }
                            }
                        }
                    }
                    else
                    {
                        // ── Project is open: manage levels ───────────────
                        ImGui::Text("Project: %s", project->name().c_str());
                        ImGui::Text("Root: %s", project->root_path().c_str());
                        ImGui::Separator();

                        // Level list
                        ImGui::SeparatorText("Levels");
                        const auto& levels = project->levels();
                        if (levels.empty())
                        {
                            ImGui::TextDisabled("No levels yet. Create one below.");
                        }
                        else
                        {
                            for (size_t i = 0; i < levels.size(); ++i)
                            {
                                ImGui::PushID(static_cast<int>(i));
                                ImGui::Text("%s", levels[i].name.c_str());
                                ImGui::SameLine();
                                ImGui::TextDisabled("(%s)", levels[i].filePath.c_str());
                                ImGui::SameLine();

                                if (ImGui::Button("Open"))
                                {
                                    std::string absPath = project->get_absolute_path(levels[i].filePath);
                                    auto result = Level::load(absPath);
                                    if (result)
                                    {
                                        enter_level_editor(std::make_unique<Level>(std::move(result.value())));
                                    }
                                    else
                                    {
                                        statusMessage = {fmt::format("Failed to load level '{}': {}", levels[i].name,
                                                                     result.error().message),
                                                         true, 5.0f};
                                    }
                                }

                                ImGui::SameLine();
                                if (ImGui::Button("Remove"))
                                {
                                    project->remove_level(i);
                                    (void)project->save();
                                    ImGui::PopID();
                                    break; // list modified, exit loop
                                }

                                ImGui::PopID();
                            }
                        }

                        // Create new level
                        ImGui::Spacing();
                        ImGui::SeparatorText("Create New Level");
                        ImGui::InputText("Level Name", newLevelNameBuf, sizeof(newLevelNameBuf));

                        if (ImGui::Button("Create Level"))
                        {
                            std::string lName{newLevelNameBuf};
                            if (lName.empty())
                            {
                                statusMessage = {"Level name is required.", true, 4.0f};
                            }
                            else
                            {
                                auto newLevel = std::make_unique<Level>(Level::create_new(lName));

                                // Save to project's Levels/ directory
                                fs::path relPath = fs::path("Levels") / (lName + ".noc_level");
                                std::string absPath = project->get_absolute_path(relPath);
                                fs::create_directories(fs::path(absPath).parent_path());

                                auto saveResult = newLevel->save_as(absPath);
                                if (saveResult)
                                {
                                    project->add_level(lName, relPath.string());
                                    (void)project->save();
                                    enter_level_editor(std::move(newLevel));
                                }
                                else
                                {
                                    statusMessage = {
                                        fmt::format("Failed to save new level: {}", saveResult.error().message), true,
                                        5.0f};
                                }
                            }
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        if (ImGui::Button("Close Project"))
                        {
                            project.reset();
                            projectAssetsDirty = true;
                            selectedProjectAsset = -1;
                            requestDefaultDockLayout = true;
                            statusMessage = {"Project closed.", false, 3.0f};
                        }
                    }

                    ImGui::Spacing();
                    draw_status_message(statusMessage, deltaTime);
                }
                ImGui::End();

                // Asset manager is also available in project browser mode.
                if (project && showAssetManagerPanel)
                {
                    if (ImGui::Begin("Asset Manager", &showAssetManagerPanel))
                    {
                        if (projectAssetsDirty)
                        {
                            projectAssetsCache = scan_project_assets(project.get());
                            projectAssetsDirty = false;
                            if (selectedProjectAsset >= static_cast<int>(projectAssetsCache.size()))
                                selectedProjectAsset = -1;
                        }

                        if (ImGui::Button("Refresh"))
                            projectAssetsDirty = true;

                        ImGui::InputTextWithHint("##AssetFilterProjectBrowser", "Filter assets...", assetFilterBuf,
                                                 sizeof(assetFilterBuf));
                        const std::string filter = to_lower_copy(assetFilterBuf);

                        ImGui::Separator();
                        ImGui::Text("Tracked assets: %zu", projectAssetsCache.size());

                        ImGuiTableFlags tableFlags =
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable;
                        if (ImGui::BeginTable("ProjectAssetsTableBrowser", 3, tableFlags, ImVec2(0.0f, 260.0f)))
                        {
                            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                            ImGui::TableSetupColumn("Path");
                            ImGui::TableSetupColumn("Size (KiB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                            ImGui::TableHeadersRow();

                            for (int i = 0; i < static_cast<int>(projectAssetsCache.size()); ++i)
                            {
                                const auto& asset = projectAssetsCache[i];
                                const std::string searchable = to_lower_copy(asset.category + " " + asset.relativePath);
                                if (!filter.empty() && searchable.find(filter) == std::string::npos)
                                    continue;

                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(asset.category.c_str());

                                ImGui::TableSetColumnIndex(1);
                                bool selected = (selectedProjectAsset == i);
                                if (ImGui::Selectable(asset.relativePath.c_str(), selected,
                                                      ImGuiSelectableFlags_SpanAllColumns))
                                    selectedProjectAsset = i;

                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%.1f", static_cast<double>(asset.sizeBytes) / 1024.0);
                            }

                            ImGui::EndTable();
                        }

                        const bool hasSelection =
                            selectedProjectAsset >= 0 && selectedProjectAsset < static_cast<int>(projectAssetsCache.size());
                        if (!hasSelection)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Delete Selected Asset"))
                        {
                            const auto& asset = projectAssetsCache[selectedProjectAsset];
                            std::error_code ec;
                            fs::path absPath = project->get_absolute_path(asset.relativePath);
                            if (fs::remove(absPath, ec))
                            {
                                statusMessage = {fmt::format("Deleted '{}'", asset.relativePath), false, 3.5f};
                                projectAssetsDirty = true;
                                selectedProjectAsset = -1;
                                assetManager.clear_models();
                                assetManager.clear_textures();
                            }
                            else
                            {
                                statusMessage = {fmt::format("Failed to delete '{}': {}", asset.relativePath,
                                                             ec.message()),
                                                 true, 5.0f};
                            }
                        }
                        if (!hasSelection)
                            ImGui::EndDisabled();
                    }
                    ImGui::End();
                }
            }

            // =============================================================
            // LEVEL EDITOR STATE
            // =============================================================
            if (editorState == EditorState::LevelEditor && level)
            {
                World& world = level->world();
                bool backToProjectsRequested = false;

                // --- Viewport ---
                if (showViewportPanel)
                {
                    ImGuiWindowFlags viewportFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                    if (ImGui::Begin("Viewport", &showViewportPanel, viewportFlags))
                    {
                        if (ImGui::SmallButton(viewport_aspect_mode_name(viewportAspectMode)))
                            ImGui::OpenPopup("ViewportAspectModePopup");
                        if (ImGui::BeginPopup("ViewportAspectModePopup"))
                        {
                            const auto add_aspect_option = [&](const char* label, ViewportAspectMode mode) {
                                const bool selected = (viewportAspectMode == mode);
                                if (ImGui::Selectable(label, selected))
                                    viewportAspectMode = mode;
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                            };
                            add_aspect_option("Free (Unlocked)", ViewportAspectMode::Free);
                            add_aspect_option("16:9", ViewportAspectMode::Aspect16x9);
                            add_aspect_option("4:3", ViewportAspectMode::Aspect4x3);
                            add_aspect_option("1:1", ViewportAspectMode::Aspect1x1);
                            add_aspect_option("21:9", ViewportAspectMode::Aspect21x9);
                            ImGui::EndPopup();
                        }

                        const ImVec2 avail = ImGui::GetContentRegionAvail();
                        ImVec2 imageSize = avail;
                        if (avail.x > 1.0f && avail.y > 1.0f && viewportAspectMode != ViewportAspectMode::Free)
                        {
                            const float targetAspect = viewport_aspect_mode_value(viewportAspectMode);
                            if (targetAspect > 0.0f)
                            {
                                const float currentAspect = avail.x / avail.y;
                                if (currentAspect > targetAspect)
                                    imageSize.x = avail.y * targetAspect;
                                else
                                    imageSize.y = avail.x / targetAspect;

                                const ImVec2 cursorPos = ImGui::GetCursorPos();
                                ImGui::SetCursorPos(ImVec2(cursorPos.x + (avail.x - imageSize.x) * 0.5f,
                                                           cursorPos.y + (avail.y - imageSize.y) * 0.5f));
                            }
                        }

                        if (viewportDescriptorSet != VK_NULL_HANDLE)
                        {
                            ImGui::Image(reinterpret_cast<ImTextureID>(viewportDescriptorSet), imageSize, ImVec2(0, 0),
                                         ImVec2(1, 1));
                            viewportHovered = ImGui::IsItemHovered();
                        }
                        else
                        {
                            ImGui::TextDisabled("Viewport texture unavailable");
                            viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleVar();
                }

                // --- Renderer Stats Overlay ---
                if (showRendererStatsPanel)
                {
                    if (ImGui::Begin("Renderer Stats", &showRendererStatsPanel, ImGuiWindowFlags_NoFocusOnAppearing))
                    {
                        uint32_t rw = renderer.get_render_width();
                        uint32_t rh = renderer.get_render_height();

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
                        ImGui::Text("Submitted Renderables: %u", vulkan.get_renderable_count());
                        ImGui::Text("Visible Renderables: %u", vulkan.get_last_visible_renderable_count());
                        ImGui::Text("Culled Renderables: %u", vulkan.get_last_culled_renderable_count());
                        ImGui::Text("Instanced Batches: %u", vulkan.get_last_instanced_batch_count());
                        ImGui::Text("Draw Calls: %u", vulkan.get_last_draw_call_count());
                        ImGui::Text("Triangles: %u", vulkan.get_total_triangle_count());
                        ImGui::Text("Meshes Loaded: %u", vulkan.get_mesh_count());
                        ImGui::Text("Textures Loaded: %u", vulkan.get_texture_count());
                        ImGui::Text("Materials Loaded: %u", vulkan.get_material_count());
                        ImGui::Text("Mesh VRAM (est): %.2f MiB", bytes_to_mib(vulkan.get_mesh_memory_bytes()));
                        ImGui::Text("Texture VRAM (est): %.2f MiB", bytes_to_mib(vulkan.get_texture_memory_bytes()));
                        ImGui::Text("Script Environments: %zu", scriptEngine.get_environment_count());
                    }
                    ImGui::End();
                }

                // --- Graphics Settings Panel ---
                if (showGraphicsPanel)
                {
                    if (ImGui::Begin("Graphics Settings", &showGraphicsPanel))
                    {
                        // MSAA
                        bool msaaChanged = false;
                        const char* msaaLabel = msaa_sample_count_name(graphicsDraft.msaaSamples);
                        if (ImGui::BeginCombo("MSAA", msaaLabel))
                        {
                            constexpr int msaaOptions[] = {1, 2, 4, 8};
                            for (int opt : msaaOptions)
                            {
                                bool isSelected = (graphicsDraft.msaaSamples == opt);
                                if (ImGui::Selectable(msaa_sample_count_name(opt), isSelected))
                                {
                                    graphicsDraft.msaaSamples = opt;
                                    graphicsDraft.dirty = true;
                                    msaaChanged = true;
                                }
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        // Render scale
                        bool renderScaleChanged =
                            ImGui::SliderFloat("Render Scale", &graphicsDraft.renderScale, 0.25f, 2.0f, "%.2f");
                        const bool renderScaleReleased = ImGui::IsItemDeactivatedAfterEdit();
                        if (renderScaleChanged)
                            graphicsDraft.dirty = true;

                        // Present mode
                        bool presentModeChanged = false;
                        int selected = static_cast<int>(graphicsDraft.presentMode);
                        ImGui::Text("Present Mode");
                        if (ImGui::RadioButton("VSync", &selected, static_cast<int>(KHR_Settings::VSync)))
                        {
                            graphicsDraft.presentMode = KHR_Settings::VSync;
                            graphicsDraft.dirty = true;
                            presentModeChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Triple Buffered", &selected,
                                               static_cast<int>(KHR_Settings::Triple_Buffering)))
                        {
                            graphicsDraft.presentMode = KHR_Settings::Triple_Buffering;
                            graphicsDraft.dirty = true;
                            presentModeChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Immediate", &selected, static_cast<int>(KHR_Settings::Immediate)))
                        {
                            graphicsDraft.presentMode = KHR_Settings::Immediate;
                            graphicsDraft.dirty = true;
                            presentModeChanged = true;
                        }

                        ImGui::Checkbox("Auto Apply", &graphicsDraft.autoApply);
                        if (graphicsDraft.autoApply && graphicsDraft.dirty)
                        {
                            // Apply MSAA/present changes immediately, but only apply render scale when the slider
                            // interaction is committed (release/enter) to avoid rapid recreate churn.
                            if (msaaChanged || presentModeChanged || renderScaleReleased)
                                graphicsApplyRequested = true;
                        }

                        if (ImGui::Button("Apply"))
                            graphicsApplyRequested = true;
                        ImGui::SameLine();
                        if (ImGui::Button("Revert"))
                            sync_graphics_draft_from_runtime();

                        if (graphicsDraft.autoApply)
                            ImGui::TextDisabled("Auto Apply is enabled.");

                        // Shader recompilation
                        ImGui::Spacing();
                        ImGui::SeparatorText("Shaders");
                        if (ImGui::Button("Recompile Shaders"))
                        {
                            renderer.wait_idle();
                            auto result = renderer.recompile_shaders();
                            if (result)
                                statusMessage = {"Shaders recompiled successfully.", false, 3.0f};
                            else
                                statusMessage =
                                    {fmt::format("Shader compilation failed: {}", result.error().message), true, 5.0f};
                        }
                    }
                    ImGui::End();
                }

                // --- Level Panel ---
                if (showLevelPanel)
                {
                    if (ImGui::Begin("Level", &showLevelPanel))
                    {
                    ImGui::Text("Level: %s%s", level->name().c_str(), level->is_dirty() ? " *" : "");

                    if (!level->file_path().empty())
                        ImGui::TextDisabled("File: %s", level->file_path().c_str());

                    if (ImGui::Button("Save"))
                    {
                        if (level->file_path().empty())
                        {
                            // If we have a project, save under the project's Levels/ directory
                            if (project)
                            {
                                fs::path relPath = fs::path("Levels") / (level->name() + ".noc_level");
                                std::string absPath = project->get_absolute_path(relPath);
                                fs::create_directories(fs::path(absPath).parent_path());
                                auto result = level->save_as(absPath);
                                if (result)
                                {
                                    // Check if this level is already registered in the project
                                    bool found = false;
                                    std::string relStr = relPath.string();
                                    for (const auto& entry : project->levels())
                                    {
                                        if (entry.filePath == relStr)
                                        {
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found)
                                    {
                                        project->add_level(level->name(), std::move(relStr));
                                        (void)project->save();
                                    }
                                    statusMessage = {"Level saved.", false, 3.0f};
                                }
                                else
                                    statusMessage = {fmt::format("Save failed: {}", result.error().message), true,
                                                     5.0f};
                            }
                            else
                            {
                                statusMessage = {"No file path set and no project open.", true, 4.0f};
                            }
                        }
                        else
                        {
                            auto result = level->save();
                            if (result)
                                statusMessage = {"Level saved.", false, 3.0f};
                            else
                                statusMessage = {fmt::format("Save failed: {}", result.error().message), true, 5.0f};
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Back to Projects"))
                        backToProjectsRequested = true;

                        draw_status_message(statusMessage, deltaTime);
                    }
                    ImGui::End();
                }

                // --- Objects Panel (Add Entity + Import Model) ---
                if (showObjectsPanel)
                {
                    if (ImGui::Begin("Objects", &showObjectsPanel))
                    {
                        // Add empty entity
                        if (ImGui::Button("Add Empty Entity"))
                        {
                            ++addedEntityCount;
                            std::string name = fmt::format("Entity_{}", addedEntityCount);
                            world.create_entity(std::move(name));
                            level->mark_dirty();
                        }

                        ImGui::Spacing();
                        ImGui::SeparatorText("Import Model");
                        ImGui::InputText("OBJ Path", importModelPathBuf, sizeof(importModelPathBuf));

                        if (ImGui::Button("Import"))
                        {
                            std::string objPath{importModelPathBuf};
                            if (objPath.empty())
                            {
                                statusMessage = {"Please enter an OBJ file path.", true, 4.0f};
                            }
                            else
                            {
                                renderer.wait_idle();
                                if (import_model(objPath, assetManager, renderer, world, statusMessage, project.get()))
                                {
                                    level->mark_dirty();
                                    projectAssetsDirty = true;
                                }
                            }
                        }
                    }
                    ImGui::End();
                }

                // --- Asset Manager Panel ---
                if (showAssetManagerPanel)
                {
                    if (ImGui::Begin("Asset Manager", &showAssetManagerPanel))
                    {
                    if (project)
                    {
                        if (projectAssetsDirty)
                        {
                            projectAssetsCache = scan_project_assets(project.get());
                            projectAssetsDirty = false;
                            if (selectedProjectAsset >= static_cast<int>(projectAssetsCache.size()))
                                selectedProjectAsset = -1;
                        }

                        if (ImGui::Button("Refresh"))
                            projectAssetsDirty = true;
                        ImGui::SameLine();
                        if (ImGui::Button("Reload Level Assets"))
                        {
                            renderer.wait_idle();
                            reload_level_assets(assetManager, renderer, world, statusMessage, project.get());
                            projectAssetsDirty = true;
                        }

                        ImGui::InputTextWithHint("##AssetFilter", "Filter assets...", assetFilterBuf, sizeof(assetFilterBuf));
                        const std::string filter = to_lower_copy(assetFilterBuf);

                        ImGui::Separator();
                        ImGui::Text("Tracked assets: %zu", projectAssetsCache.size());

                        ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
                        if (ImGui::BeginTable("ProjectAssetsTable", 3, tableFlags, ImVec2(0.0f, 220.0f)))
                        {
                            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                            ImGui::TableSetupColumn("Path");
                            ImGui::TableSetupColumn("Size (KiB)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                            ImGui::TableHeadersRow();

                            for (int i = 0; i < static_cast<int>(projectAssetsCache.size()); ++i)
                            {
                                const auto& asset = projectAssetsCache[i];
                                const std::string searchable = to_lower_copy(asset.category + " " + asset.relativePath);
                                if (!filter.empty() && searchable.find(filter) == std::string::npos)
                                    continue;

                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(asset.category.c_str());

                                ImGui::TableSetColumnIndex(1);
                                bool selected = (selectedProjectAsset == i);
                                if (ImGui::Selectable(asset.relativePath.c_str(), selected,
                                                      ImGuiSelectableFlags_SpanAllColumns))
                                    selectedProjectAsset = i;

                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%.1f", static_cast<double>(asset.sizeBytes) / 1024.0);
                            }

                            ImGui::EndTable();
                        }

                        const bool hasSelection =
                            selectedProjectAsset >= 0 && selectedProjectAsset < static_cast<int>(projectAssetsCache.size());
                        if (!hasSelection)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Delete Selected Asset"))
                        {
                            const auto& asset = projectAssetsCache[selectedProjectAsset];
                            std::error_code ec;
                            fs::path absPath = project->get_absolute_path(asset.relativePath);
                            if (fs::remove(absPath, ec))
                            {
                                statusMessage = {fmt::format("Deleted '{}'", asset.relativePath), false, 3.5f};
                                projectAssetsDirty = true;
                                selectedProjectAsset = -1;
                                assetManager.clear_models();
                                assetManager.clear_textures();
                                level->mark_dirty();
                            }
                            else
                            {
                                statusMessage =
                                    {fmt::format("Failed to delete '{}': {}", asset.relativePath, ec.message()), true, 5.0f};
                            }
                        }
                        if (!hasSelection)
                            ImGui::EndDisabled();
                    }
                        else
                        {
                            ImGui::TextDisabled("Open a project to manage imported assets.");
                        }
                    }
                    ImGui::End();
                }

                // --- Scene Hierarchy ---
                if (showSceneHierarchyPanel)
                {
                    if (ImGui::Begin("Scene Hierarchy", &showSceneHierarchyPanel))
                    {
                        const auto& roots = world.get_root_entities();
                        for (entt::entity root : roots)
                            draw_entity_hierarchy(world, root, selectedEntity);
                    }
                    ImGui::End();
                }

                // --- Inspector ---
                if (showInspectorPanel)
                {
                    if (ImGui::Begin("Inspector", &showInspectorPanel))
                    {
                        if (selectedEntity != entt::null && world.registry().valid(selectedEntity))
                        {
                            const auto& nameComp = world.registry().get<NameComponent>(selectedEntity);
                            ImGui::Text("Entity: %s", nameComp.name.c_str());

                            if (const auto* mc = world.registry().try_get<MeshComponent>(selectedEntity))
                            {
                                if (mc->meshIndex >= 0)
                                    ImGui::Text("Mesh Index: %d", mc->meshIndex);
                                ImGui::Text("Material Index: %d", mc->materialIndex);
                                if (!mc->assetPath.empty())
                                    ImGui::TextDisabled("Asset: %s", mc->assetPath.c_str());
                            }

                        if (world.registry().any_of<CameraComponent>(selectedEntity))
                        {
                            ImGui::Separator();
                            auto& cam = world.registry().get<CameraComponent>(selectedEntity);
                            ImGui::Text("Camera (active: %s)", cam.isActive ? "yes" : "no");
                            ImGui::DragFloat("FOV", &cam.fov, 0.5f, 10.0f, 120.0f);
                            ImGui::DragFloat("Near", &cam.nearPlane, 0.01f, 0.001f, 10.0f);
                            ImGui::DragFloat("Far", &cam.farPlane, 1.0f, 10.0f, 10000.0f);
                        }

                        // Script component section
                        if (auto* sc = world.registry().try_get<ScriptComponent>(selectedEntity))
                        {
                            ImGui::Separator();
                            ImGui::Text("Script: %s", sc->scriptPath.empty() ? "(none)" : sc->scriptPath.c_str());

                            if (ImGui::Button("Reload Script"))
                            {
                                auto result = scriptEngine.reload_script(world, selectedEntity);
                                if (result)
                                    statusMessage = {"Script reloaded.", false, 3.0f};
                                else
                                    statusMessage = {fmt::format("Reload failed: {}", result.error().message), true,
                                                     5.0f};
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Remove Script"))
                            {
                                scriptEngine.on_entity_destroyed(world, selectedEntity);
                                world.registry().remove<ScriptComponent>(selectedEntity);
                                level->mark_dirty();
                            }
                        }
                        else
                        {
                            ImGui::Separator();

                            static std::vector<std::string> availableScripts;
                            static int selectedScriptIdx = 0;

                            // Auto-scan on first use
                            if (availableScripts.empty())
                                availableScripts = scan_available_scripts(project.get());

                            if (!availableScripts.empty())
                            {
                                if (selectedScriptIdx >= static_cast<int>(availableScripts.size()))
                                    selectedScriptIdx = 0;

                                if (ImGui::BeginCombo("Script", availableScripts[selectedScriptIdx].c_str()))
                                {
                                    for (int i = 0; i < static_cast<int>(availableScripts.size()); ++i)
                                    {
                                        bool isSelected = (selectedScriptIdx == i);
                                        if (ImGui::Selectable(availableScripts[i].c_str(), isSelected))
                                            selectedScriptIdx = i;
                                        if (isSelected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                if (ImGui::Button("Attach"))
                                {
                                    auto& newSc = world.registry().emplace<ScriptComponent>(selectedEntity);
                                    newSc.scriptPath = availableScripts[selectedScriptIdx];
                                    level->mark_dirty();
                                }
                                ImGui::SameLine();
                            }
                            else
                            {
                                ImGui::TextDisabled("No .lua scripts found");
                            }

                            if (ImGui::Button("Scan Scripts"))
                            {
                                availableScripts = scan_available_scripts(project.get());
                                selectedScriptIdx = 0;
                            }
                        }

                        ImGui::Separator();
                        auto& tc = world.registry().get<TransformComponent>(selectedEntity);
                        draw_transform_inspector(tc);

                        ImGui::Separator();

                        if (ImGui::Button("Delete Entity"))
                        {
                            // Don't delete the active camera
                            bool isActiveCamera = false;
                            if (auto* cam = world.registry().try_get<CameraComponent>(selectedEntity))
                                isActiveCamera = cam->isActive;

                            if (isActiveCamera)
                            {
                                statusMessage = {"Cannot delete the active camera entity.", true, 4.0f};
                            }
                            else
                            {
                                scriptEngine.on_entity_tree_destroyed(world, selectedEntity);
                                world.destroy_entity(selectedEntity);
                                selectedEntity = entt::null;
                                level->mark_dirty();
                            }
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("Deselect"))
                            selectedEntity = entt::null;
                    }
                        else
                        {
                            ImGui::TextDisabled("Select an entity in the Scene Hierarchy");
                        }
                    }
                    ImGui::End();
                }

                if (backToProjectsRequested)
                    enter_project_browser();
            }

            ImGui::Render();
            return renderer.draw_frame();
        }));
        code != 0)
    {
        release_viewport_texture();
        Editor::UI::Shutdown(vulkan);
        ImGui::DestroyContext();
        return code;
    }

    renderer.wait_idle();
    if (level)
        scriptEngine.on_world_destroyed(level->world());
    scriptEngine.shutdown();
    release_viewport_texture();
    Editor::UI::Shutdown(vulkan);
    ImGui::DestroyContext();

    return 0;
}
