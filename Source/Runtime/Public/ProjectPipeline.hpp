#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct MaterialData;
class AssetManager;
class IRenderer;
class Level;
class PhysicsWorld;
class Project;
class ScriptEngine;

NOC_SUPPRESS_DLL_WARNINGS

struct NOC_EXPORT RuntimeLoadOptions
{
    bool seedProjectDefaults{false};
    bool allowEmbeddedMaterialTextureExtraction{true};
    bool requireCookedModels{false};
    bool requireGlowShaders{true};
};

struct NOC_EXPORT RuntimeLoadReport
{
    std::uint32_t totalMeshes{};
    std::uint32_t totalMaterials{};
    bool usedProjectShaders{false};
    bool usedRawSourceAssets{false};
    std::vector<std::string> warnings{};
};

struct NOC_EXPORT CookProjectOptions
{
    std::filesystem::path outputRoot{};
    bool overwriteOutput{true};
    bool compileShaders{true};
    bool strict{true};
};

struct NOC_EXPORT CookProjectResult
{
    std::filesystem::path cookedProjectFile{};
    std::uint32_t cookedLevelCount{};
    std::uint32_t cookedModelCount{};
    std::uint32_t copiedTextureCount{};
    std::uint32_t copiedMaterialCount{};
    std::uint32_t copiedScriptCount{};
    std::uint32_t copiedEngineFileCount{};
    std::vector<std::string> warnings{};
};

struct NOC_EXPORT BundleProjectOptions
{
    std::filesystem::path outputRoot{};
    std::filesystem::path runtimeBinaryRoot{};
    bool overwriteOutput{true};
    bool compileShaders{true};
    bool strict{true};
};

struct NOC_EXPORT BundleProjectResult
{
    std::filesystem::path bundleRoot{};
    std::filesystem::path contentRoot{};
    CookProjectResult cookResult{};
    std::uint32_t copiedRuntimeFileCount{};
    std::vector<std::string> warnings{};
};

NOC_EXPORT Result<> clear_runtime_scene(
    IRenderer& renderer,
    AssetManager& assetManager,
    ScriptEngine& scriptEngine,
    PhysicsWorld& physicsWorld,
    Level* loadedLevel
);

NOC_EXPORT Result<RuntimeLoadReport> prepare_loaded_level(
    AssetManager& assetManager,
    IRenderer& renderer,
    ScriptEngine& scriptEngine,
    PhysicsWorld& physicsWorld,
    Level& level,
    Project* project,
    const RuntimeLoadOptions& options = {}
);

NOC_EXPORT std::vector<std::string> scan_available_scripts(const Project* project);

NOC_EXPORT bool load_material_from_file(
    const std::filesystem::path& filePath,
    std::string& outName,
    MaterialData& outData,
    const Project* project,
    bool allowTextureExtraction
);

NOC_EXPORT std::vector<std::filesystem::path> scan_material_files(const Project* project);

NOC_EXPORT Result<CookProjectResult> cook_project(
    const std::filesystem::path& projectFilePath,
    const CookProjectOptions& options
);

NOC_EXPORT Result<BundleProjectResult> bundle_project(
    const std::filesystem::path& projectFilePath,
    const BundleProjectOptions& options
);

NOC_RESTORE_DLL_WARNINGS
