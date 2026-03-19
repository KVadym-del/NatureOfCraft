#include "../Public/ProjectPipeline.hpp"

#include "../../Assets/Private/ModelLoader.hpp"
#include "../../Assets/Private/TextureLoader.hpp"
#include "../../Assets/Public/AssetManager.hpp"
#include "../../Assets/Public/MaterialData.hpp"
#include "../../Assets/Public/ModelData.hpp"
#include "../../Assets/Public/TextureData.hpp"
#include "../../Core/Public/RuntimePaths.hpp"
#include "../../ECS/Public/Components.hpp"
#include "../../ECS/Public/World.hpp"
#include "../../Level/Public/Level.hpp"
#include "../../Level/Public/Project.hpp"
#include "../../Physics/Public/PhysicsWorld.hpp"
#include "../../Rendering/Public/IRenderer.hpp"
#include "../../Rendering/Public/ShaderCompiler.hpp"
#include "../../Scripting/Public/ScriptEngine.hpp"

#include <MaterialAsset_generated.h>
#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace
{
struct MeshGpuInfo
{
    std::int32_t gpuMeshIndex{-1};
    std::int32_t gpuMaterialIndex{0};
};

std::string to_lower_copy(std::string s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return s;
}

bool file_contains(const std::filesystem::path& path, std::string_view needle)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

std::filesystem::path resolve_asset_path(const std::filesystem::path& path, const Project* project)
{
    if (path.empty())
        return {};
    if (path.is_absolute())
        return path;
    if (project)
        return std::filesystem::path(project->get_absolute_path(path));
    return path;
}

Result<> ensure_parent_directory(const std::filesystem::path& filePath)
{
    std::error_code ec{};
    std::filesystem::create_directories(filePath.parent_path(), ec);
    if (ec)
    {
        return make_error(
            fmt::format("Failed to create directory '{}': {}", filePath.parent_path().string(), ec.message()),
            ErrorCode::AssetCacheWriteFailed
        );
    }
    return {};
}

Result<> copy_file_if_needed(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationPath,
    bool overwriteExisting
)
{
    if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
        return make_error(
            fmt::format("Missing source file '{}'", sourcePath.string()),
            ErrorCode::AssetFileNotFound
        );

    if (auto dirResult = ensure_parent_directory(destinationPath); !dirResult)
        return dirResult;

    std::error_code ec{};
    const auto options = overwriteExisting ? std::filesystem::copy_options::overwrite_existing
                                           : std::filesystem::copy_options::skip_existing;
    std::filesystem::copy_file(sourcePath, destinationPath, options, ec);
    if (ec)
    {
        return make_error(
            fmt::format("Failed to copy '{}' to '{}': {}", sourcePath.string(), destinationPath.string(), ec.message()),
            ErrorCode::AssetCacheWriteFailed
        );
    }

    return {};
}

std::vector<std::filesystem::path> collect_model_texture_load_paths(const ModelData& model, const Project* project)
{
    std::set<std::filesystem::path> uniqueTexturePaths{};
    for (const auto& mat : model.materials)
    {
        for (const auto& texturePath : {
                    mat.albedoTexturePath,
                    mat.normalTexturePath,
                    mat.roughnessTexturePath,
                    mat.metallicTexturePath,
                    mat.aoTexturePath
                }
            )
        {
            if (const std::filesystem::path resolvedPath = resolve_asset_path(texturePath, project); !resolvedPath.empty())
                uniqueTexturePaths.insert(resolvedPath);
        }
    }

    return {uniqueTexturePaths.begin(), uniqueTexturePaths.end()};
}

std::unordered_map<std::filesystem::path, entt::resource<TextureData>> prepare_texture_handles_for_paths(AssetManager& assetManager, const std::vector<std::filesystem::path>& texturePaths)
{
    std::set<std::filesystem::path> uniqueTexturePaths(texturePaths.begin(), texturePaths.end());
    std::vector<std::filesystem::path> uniquePaths(uniqueTexturePaths.begin(), uniqueTexturePaths.end());
    std::unordered_map<std::filesystem::path, entt::resource<TextureData>> textureHandles;
    textureHandles.reserve(uniquePaths.size());

    std::vector<std::size_t> decodeIndices{};
    decodeIndices.reserve(uniquePaths.size());
    std::vector<std::shared_ptr<TextureData>> decoded(uniquePaths.size());
    std::vector<Error> decodeErrors(uniquePaths.size());

    for (std::size_t i = 0; i < uniquePaths.size(); ++i)
    {
        const std::string pathString = uniquePaths[i].string();
        if (assetManager.contains_texture(pathString))
        {
            auto handle = assetManager.load_texture(pathString);
            if (handle)
                textureHandles.emplace(uniquePaths[i], handle);
            continue;
        }

        decodeIndices.push_back(i);
    }

    if (!decodeIndices.empty())
    {
        tf::Taskflow taskflow{};
        for (std::size_t taskIndex = 0; taskIndex < decodeIndices.size(); ++taskIndex)
        {
            taskflow.emplace([&, taskIndex]() {
                const std::size_t pathIndex = decodeIndices[taskIndex];
                auto loadResult = TextureLoader::load_image(uniquePaths[pathIndex].string());
                if (loadResult)
                    decoded[pathIndex] = std::move(loadResult.value());
                else
                    decodeErrors[pathIndex] = loadResult.error();
            });
        }

        assetManager.get_executor().run(taskflow).wait();

        for (std::size_t pathIndex : decodeIndices)
        {
            if (!decoded[pathIndex])
                continue;

            auto handle = assetManager.load_texture(uniquePaths[pathIndex].string(), *decoded[pathIndex]);
            if (handle)
                textureHandles.emplace(uniquePaths[pathIndex], handle);
        }
    }

    return textureHandles;
}

std::uint32_t upload_material_texture(
    const std::filesystem::path& texturePath,
    const Project* project,
    AssetManager& assetManager,
    IRenderer& renderer,
    const std::unordered_map<std::filesystem::path, entt::resource<TextureData>>& preparedTextureHandles,
    std::unordered_map<std::filesystem::path, std::uint32_t>& uploadedTextureIndices
)
{
    const std::filesystem::path resolvedPath = resolve_asset_path(texturePath, project);
    if (resolvedPath.empty())
        return 0;

    if (const auto it = uploadedTextureIndices.find(resolvedPath); it != uploadedTextureIndices.end())
        return it->second;

    entt::resource<TextureData> textureHandle{};
    if (const auto preparedIt = preparedTextureHandles.find(resolvedPath); preparedIt != preparedTextureHandles.end())
        textureHandle = preparedIt->second;
    else
        textureHandle = assetManager.load_texture(resolvedPath.string());

    if (!textureHandle)
        return 0;

    auto textureResult = renderer.upload_texture(*textureHandle);
    if (!textureResult)
        return 0;

    uploadedTextureIndices.emplace(resolvedPath, textureResult.value());
    return textureResult.value();
}

Result<> seed_project_defaults(const Project& project)
{
    const RuntimePaths* runtimePaths = RuntimePaths::try_current();
    if (!runtimePaths)
        return {};

    std::error_code ec{};
    const std::filesystem::path shadersDir = std::filesystem::path(project.root_path()) / "Assets" / "Shaders";
    const std::filesystem::path scriptsDir = std::filesystem::path(project.root_path()) / "Scripts";
    std::filesystem::create_directories(shadersDir, ec);
    ec.clear();
    std::filesystem::create_directories(scriptsDir, ec);

    for (const auto& shaderName : {"shader.vert", "shader.frag"})
    {
        const std::filesystem::path sourcePath = runtimePaths->resolve_engine_resource(shaderName);
        if (!std::filesystem::exists(sourcePath))
            continue;

        ec.clear();
        std::filesystem::copy_file(sourcePath, shadersDir / shaderName, std::filesystem::copy_options::skip_existing, ec);
    }

    for (const auto& scriptName : {"spin.lua", "pulse_glow.lua", "outline_xray.lua"})
    {
        const std::filesystem::path sourcePath = runtimePaths->resolve_engine_resource(std::filesystem::path("scripts") / scriptName);
        if (!std::filesystem::exists(sourcePath))
            continue;

        ec.clear();
        std::filesystem::copy_file(sourcePath, scriptsDir / scriptName, std::filesystem::copy_options::skip_existing, ec);
    }

    return {};
}

Result<RuntimeLoadReport> reload_level_assets(
    AssetManager& assetManager,
    IRenderer& renderer,
    World& world,
    Project* project,
    bool requireCookedModels
)
{
    auto& reg = world.registry();
    RuntimeLoadReport report{};

    std::set<std::string> uniquePaths{};
    auto meshView = reg.view<MeshComponent>();
    for (auto entity : meshView)
    {
        const auto& meshComponent = meshView.get<MeshComponent>(entity);
        if (!meshComponent.assetPath.empty())
            uniquePaths.insert(meshComponent.assetPath);
    }

    if (uniquePaths.empty())
        return report;

    std::unordered_map<std::string, std::unordered_map<std::string, MeshGpuInfo>> assetMeshMap;
    struct LoadedModelEntry
    {
        std::string assetPath;
        entt::resource<ModelData> modelHandle;
    };

    std::vector<LoadedModelEntry> loadedModels{};
    loadedModels.reserve(uniquePaths.size());

    std::set<std::filesystem::path> uniqueTextureLoadPaths{};
    for (const auto& assetPath : uniquePaths)
    {
        const std::filesystem::path resolvedPath = resolve_asset_path(assetPath, project);
        if (requireCookedModels && resolvedPath.extension() != ".noc_model")
        {
            return make_error(
                fmt::format("Shipping runtime requires cooked '.noc_model' assets, got '{}'", assetPath),
                ErrorCode::AssetInvalidData
            );
        }

        if (resolvedPath.extension() != ".noc_model")
            report.usedRawSourceAssets = true;

        entt::resource<ModelData> modelHandle = assetManager.load_model(resolvedPath.string());
        if (!modelHandle)
        {
            report.warnings.push_back(fmt::format("Failed to reload model '{}'", resolvedPath.string()));
            continue;
        }

        for (const auto& texturePath : collect_model_texture_load_paths(*modelHandle, project))
            uniqueTextureLoadPaths.insert(texturePath);

        loadedModels.push_back({assetPath, modelHandle});
    }

    const std::vector<std::filesystem::path> allTextureLoadPaths(uniqueTextureLoadPaths.begin(), uniqueTextureLoadPaths.end());
    const auto preparedTextureHandles = prepare_texture_handles_for_paths(assetManager, allTextureLoadPaths);
    std::unordered_map<std::filesystem::path, std::uint32_t> uploadedTextureIndices;
    uploadedTextureIndices.reserve(preparedTextureHandles.size());

    for (const auto& loadedModel : loadedModels)
    {
        const ModelData& model = *loadedModel.modelHandle;
        std::vector<std::uint32_t> gpuMaterialIndices{};
        gpuMaterialIndices.reserve(model.materials.size());

        for (const auto& material : model.materials)
        {
            const std::uint32_t albedoIndex = upload_material_texture(
                material.albedoTexturePath,
                project,
                assetManager,
                renderer, 
                preparedTextureHandles,
                uploadedTextureIndices
            );
            const std::uint32_t normalIndex = upload_material_texture(
                material.normalTexturePath,
                project, 
                assetManager,
                renderer,
                preparedTextureHandles,
                uploadedTextureIndices
            );
            const std::uint32_t roughnessIndex = upload_material_texture(
                material.roughnessTexturePath,
                project,
                assetManager,
                renderer,
                preparedTextureHandles,
                uploadedTextureIndices
            );
            const std::uint32_t metallicIndex = upload_material_texture(
                material.metallicTexturePath,
                project, 
                assetManager, 
                renderer, 
                preparedTextureHandles,
                uploadedTextureIndices
            );
            const std::uint32_t aoIndex = upload_material_texture(
                material.aoTexturePath,
                project, 
                assetManager, 
                renderer, 
                preparedTextureHandles,
                uploadedTextureIndices
            );

            auto materialResult = renderer.upload_material(albedoIndex, normalIndex, roughnessIndex, metallicIndex, aoIndex);
            gpuMaterialIndices.push_back(materialResult ? materialResult.value() : 0);
        }

        report.totalMaterials += static_cast<std::uint32_t>(gpuMaterialIndices.size());
        auto& nameMap = assetMeshMap[loadedModel.assetPath];
        for (std::size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex)
        {
            auto uploadResult = renderer.upload_mesh(model.meshes[meshIndex]);
            if (!uploadResult)
            {
                report.warnings.push_back(
                    fmt::format("Failed to upload mesh '{}': {}", model.meshes[meshIndex].name, uploadResult.error().message));
                continue;
            }

            MeshGpuInfo info{};
            info.gpuMeshIndex = static_cast<std::int32_t>(uploadResult.value());
            const std::int32_t materialMapIndex = model.meshMaterialIndices[meshIndex];
            if (materialMapIndex >= 0 && static_cast<std::size_t>(materialMapIndex) < gpuMaterialIndices.size())
                info.gpuMaterialIndex = static_cast<std::int32_t>(gpuMaterialIndices[materialMapIndex]);

            std::string meshName = model.meshes[meshIndex].name.empty() ? fmt::format("SubMesh_{}", meshIndex) : model.meshes[meshIndex].name;
            nameMap[meshName] = info;
            ++report.totalMeshes;
        }
    }

    for (auto entity : meshView)
    {
        auto& meshComponent = meshView.get<MeshComponent>(entity);
        if (meshComponent.assetPath.empty())
            continue;

        const auto assetIt = assetMeshMap.find(meshComponent.assetPath);
        if (assetIt == assetMeshMap.end())
        {
            meshComponent.meshIndex = -1;
            continue;
        }

        const auto* nameComponent = reg.try_get<NameComponent>(entity);
        if (!nameComponent)
        {
            meshComponent.meshIndex = -1;
            continue;
        }

        const auto meshIt = assetIt->second.find(nameComponent->name);
        if (meshIt == assetIt->second.end())
        {
            meshComponent.meshIndex = -1;
            report.warnings.push_back(
                fmt::format("Entity '{}' has assetPath '{}' but no matching mesh name", nameComponent->name,
                            meshComponent.assetPath));
            continue;
        }

        meshComponent.meshIndex = meshIt->second.gpuMeshIndex;
        meshComponent.materialIndex = meshIt->second.gpuMaterialIndex;
    }

    world.mark_renderables_dirty();
    return report;
}

RuntimeLoadReport load_project_material_overrides(AssetManager& assetManager,
                                                  IRenderer& renderer,
                                                  World& world,
                                                  Project* project,
                                                  bool allowTextureExtraction)
{
    RuntimeLoadReport report;
    if (!project)
        return report;

    struct LoadedMaterial
    {
        std::string name;
        MaterialData data;
        std::int32_t gpuIndex{-1};
    };

    std::vector<LoadedMaterial> materials;
    for (const auto& materialFile : scan_material_files(project))
    {
        LoadedMaterial entry;
        if (!load_material_from_file(materialFile, entry.name, entry.data, project, allowTextureExtraction))
        {
            report.warnings.push_back(fmt::format("Failed to load material '{}'", materialFile.string()));
            continue;
        }
        materials.push_back(std::move(entry));
    }

    auto meshView = world.registry().view<MeshComponent>();
    for (auto& material : materials)
    {
        bool isReferenced = false;
        for (auto entity : meshView)
        {
            const auto& meshComponent = meshView.get<MeshComponent>(entity);
            if (meshComponent.materialName == material.name)
            {
                isReferenced = true;
                break;
            }
        }

        if (!isReferenced)
            continue;

        auto uploadTexture = [&](const std::filesystem::path& texturePath) -> std::uint32_t {
            const std::filesystem::path absolutePath = resolve_asset_path(texturePath, project);
            if (absolutePath.empty() || !std::filesystem::exists(absolutePath))
                return 0;

            auto textureHandle = assetManager.load_texture(absolutePath);
            if (!textureHandle)
                return 0;

            auto textureResult = renderer.upload_texture(*textureHandle);
            return textureResult ? textureResult.value() : 0;
        };

        auto materialResult = renderer.upload_material(uploadTexture(material.data.albedoTexturePath),
                                                       uploadTexture(material.data.normalTexturePath),
                                                       uploadTexture(material.data.roughnessTexturePath),
                                                       uploadTexture(material.data.metallicTexturePath),
                                                       uploadTexture(material.data.aoTexturePath));
        if (materialResult)
            material.gpuIndex = static_cast<std::int32_t>(materialResult.value());
    }

    for (auto entity : meshView)
    {
        auto& meshComponent = meshView.get<MeshComponent>(entity);
        if (meshComponent.materialName.empty())
            continue;

        for (const auto& material : materials)
        {
            if (material.name == meshComponent.materialName && material.gpuIndex >= 0)
            {
                meshComponent.materialIndex = material.gpuIndex;
                break;
            }
        }
    }

    world.mark_renderables_dirty();
    return report;
}

Result<> copy_tree_if_exists(const std::filesystem::path& sourceRoot, const std::filesystem::path& destinationRoot)
{
    std::error_code ec;
    if (!std::filesystem::exists(sourceRoot, ec))
        return {};

    std::filesystem::create_directories(destinationRoot, ec);
    if (ec)
        return make_error(fmt::format("Failed to create directory '{}': {}", destinationRoot.string(), ec.message()),
                          ErrorCode::AssetCacheWriteFailed);

    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(sourceRoot, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (ec)
            break;

        const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), sourceRoot, ec);
        if (ec)
            break;

        const std::filesystem::path destinationPath = destinationRoot / relativePath;
        if (entry.is_directory())
        {
            std::filesystem::create_directories(destinationPath, ec);
            if (ec)
                break;
            continue;
        }

        if (!entry.is_regular_file())
            continue;

        std::filesystem::create_directories(destinationPath.parent_path(), ec);
        if (ec)
            break;

        std::filesystem::copy_file(entry.path(), destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            break;
    }

    if (ec)
        return make_error(fmt::format("Failed to copy '{}' to '{}': {}", sourceRoot.string(), destinationRoot.string(), ec.message()),
                          ErrorCode::AssetCacheWriteFailed);

    return {};
}

std::filesystem::path make_unique_cooked_model_path(const std::filesystem::path& outputRoot,
                                                    std::string_view baseName,
                                                    std::size_t uniqueIndex)
{
    std::string stem(baseName);
    if (stem.empty())
        stem = "Model";

    std::filesystem::path relativePath = std::filesystem::path("Assets") / "Models" / (stem + ".noc_model");
    std::filesystem::path absolutePath = outputRoot / relativePath;
    if (!std::filesystem::exists(absolutePath))
        return relativePath;

    return std::filesystem::path("Assets") / "Models" / fmt::format("{}_{}.noc_model", stem, uniqueIndex);
}

Result<std::filesystem::path> copy_texture_for_cook(const std::filesystem::path& sourcePath,
                                                    const std::filesystem::path& outputRoot,
                                                    CookProjectResult& result)
{
    if (sourcePath.empty())
        return std::filesystem::path{};

    const std::filesystem::path relativePath = std::filesystem::path("Assets") / "Textures" / sourcePath.filename();
    const std::filesystem::path destinationPath = outputRoot / relativePath;
    const bool destinationExists = std::filesystem::exists(destinationPath);
    if (auto copyResult = copy_file_if_needed(sourcePath, destinationPath, false); !copyResult)
        return make_error(copyResult.error());

    if (!destinationExists)
        ++result.copiedTextureCount;
    return relativePath;
}

Result<std::filesystem::path> copy_project_relative_file_for_cook(const std::filesystem::path& sourcePath,
                                                                  const std::filesystem::path& projectRoot,
                                                                  const std::filesystem::path& outputRoot,
                                                                  std::uint32_t& copiedCount)
{
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(sourcePath, projectRoot, ec);
    if (ec || relativePath.empty() || relativePath.string().starts_with(".."))
    {
        return make_error(fmt::format("Failed to map '{}' under project root '{}'", sourcePath.string(), projectRoot.string()),
                          ErrorCode::AssetInvalidData);
    }

    const std::filesystem::path destinationPath = outputRoot / relativePath;
    const bool destinationExists = std::filesystem::exists(destinationPath);
    if (auto copyResult = copy_file_if_needed(sourcePath, destinationPath, false); !copyResult)
        return make_error(copyResult.error());

    if (!destinationExists)
        ++copiedCount;
    return relativePath;
}

std::set<std::string> collect_referenced_script_paths(const World& world)
{
    std::set<std::string> scriptPaths;
    auto view = world.registry().view<ScriptComponent>();
    for (auto entity : view)
    {
        const auto& scriptComponent = view.get<ScriptComponent>(entity);
        if (!scriptComponent.scriptPath.empty())
            scriptPaths.insert(scriptComponent.scriptPath);
    }
    return scriptPaths;
}

std::set<std::string> collect_referenced_material_names(const World& world)
{
    std::set<std::string> materialNames;
    auto view = world.registry().view<MeshComponent>();
    for (auto entity : view)
    {
        const auto& meshComponent = view.get<MeshComponent>(entity);
        if (!meshComponent.materialName.empty())
            materialNames.insert(meshComponent.materialName);
    }
    return materialNames;
}

struct ProjectMaterialAsset
{
    std::filesystem::path filePath;
    MaterialData data;
};

std::unordered_map<std::string, ProjectMaterialAsset> build_project_material_lookup(const Project& project,
                                                                                    CookProjectResult& result)
{
    std::unordered_map<std::string, ProjectMaterialAsset> materialLookup;
    for (const auto& materialFile : scan_material_files(&project))
    {
        std::string materialName;
        MaterialData materialData;
        if (!load_material_from_file(materialFile, materialName, materialData, &project, false))
        {
            result.warnings.push_back(fmt::format("Failed to load material '{}'", materialFile.string()));
            continue;
        }

        materialLookup.try_emplace(materialName, ProjectMaterialAsset{materialFile, std::move(materialData)});
    }

    return materialLookup;
}

Result<> copy_engine_runtime_resources(const RuntimePaths& runtimePaths,
                                       const std::filesystem::path& engineOutputRoot,
                                       bool compileShaders,
                                       CookProjectResult& result)
{
    const std::filesystem::path engineSourceRoot =
        std::filesystem::exists(runtimePaths.engine_resources_dir()) ? runtimePaths.engine_resources_dir()
                                                                     : runtimePaths.legacy_resources_dir();

    const std::array<std::filesystem::path, 5> requiredEngineFiles{
        std::filesystem::path("shader.vert"),
        std::filesystem::path("shader.frag"),
        std::filesystem::path("NIS") / "NIS_Main.glsl",
        std::filesystem::path("NIS") / "NIS_Scaler.h",
        std::filesystem::path("NIS") / "NIS_Config.h",
    };

    for (const auto& relativePath : requiredEngineFiles)
    {
        const std::filesystem::path sourcePath = engineSourceRoot / relativePath;
        const std::filesystem::path destinationPath = engineOutputRoot / relativePath;
        const bool destinationExists = std::filesystem::exists(destinationPath);
        if (auto copyResult = copy_file_if_needed(sourcePath, destinationPath, true); !copyResult)
            return make_error(copyResult.error());

        if (!destinationExists)
            ++result.copiedEngineFileCount;
    }

    if (!compileShaders)
        return {};

    for (const auto& shaderName : {"shader.vert", "shader.frag"})
    {
        const std::filesystem::path shaderPath = engineOutputRoot / shaderName;
        auto compileResult = ShaderCompiler::compile_to_file(shaderPath, ShaderCompiler::get_spv_path(shaderPath));
        if (!compileResult)
            return make_error(compileResult.error());
    }

    const std::filesystem::path nisSource = engineOutputRoot / "NIS" / "NIS_Main.glsl";
    auto computeResult =
        ShaderCompiler::compile_compute_to_file(nisSource, ShaderCompiler::get_spv_path(nisSource), {nisSource.parent_path()});
    if (!computeResult)
        return make_error(computeResult.error());

    return {};
}

Result<> copy_referenced_material_asset(const std::string& materialName,
                                        const Project& project,
                                        const std::unordered_map<std::string, ProjectMaterialAsset>& materialLookup,
                                        const std::filesystem::path& gameOutputRoot,
                                        CookProjectResult& result)
{
    const auto it = materialLookup.find(materialName);
    if (it == materialLookup.end())
        return make_error(fmt::format("Referenced material '{}' was not found in the project", materialName),
                          ErrorCode::AssetFileNotFound);

    const ProjectMaterialAsset& materialAsset = it->second;
    auto materialCopyResult =
        copy_project_relative_file_for_cook(materialAsset.filePath, project.root_path(), gameOutputRoot, result.copiedMaterialCount);
    if (!materialCopyResult)
        return make_error(materialCopyResult.error());

    for (const auto* texturePath : {&materialAsset.data.albedoTexturePath, &materialAsset.data.normalTexturePath,
                                    &materialAsset.data.roughnessTexturePath, &materialAsset.data.metallicTexturePath,
                                    &materialAsset.data.aoTexturePath})
    {
        const std::filesystem::path resolvedTexturePath = resolve_asset_path(*texturePath, &project);
        if (resolvedTexturePath.empty())
            continue;

        auto textureCopyResult = copy_texture_for_cook(resolvedTexturePath, gameOutputRoot, result);
        if (!textureCopyResult)
            return make_error(textureCopyResult.error());
    }

    return {};
}

Result<std::filesystem::path> detect_runtime_binary_root()
{
    const RuntimePaths* runtimePaths = RuntimePaths::try_current();
    if (!runtimePaths)
        return make_error("RuntimePaths is not initialized", ErrorCode::AssetFileNotFound);

    const std::array<std::filesystem::path, 2> candidates{
        runtimePaths->executable_dir().parent_path() / "Game",
        runtimePaths->executable_dir(),
    };

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate / "Game.exe"))
            return candidate;
    }

    return make_error("Failed to locate the Game runtime binaries. Pass --runtime-dir explicitly.",
                      ErrorCode::AssetFileNotFound);
}
 
} // namespace

Result<> clear_runtime_scene(IRenderer& renderer,
                             AssetManager& assetManager,
                             ScriptEngine& scriptEngine,
                             PhysicsWorld& physicsWorld,
                             Level* loadedLevel)
{
    renderer.wait_idle();
    renderer.set_renderables({});
    physicsWorld.clear();

    if (loadedLevel)
        scriptEngine.on_world_destroyed(loadedLevel->world());

    if (auto clearResult = renderer.clear_scene_content(); !clearResult)
        return make_error(clearResult.error());

    assetManager.clear_materials();
    assetManager.clear_textures();
    assetManager.clear_models();
    assetManager.clear_meshes();
    return {};
}

Result<RuntimeLoadReport> prepare_loaded_level(AssetManager& assetManager,
                                               IRenderer& renderer,
                                               ScriptEngine& scriptEngine,
                                               PhysicsWorld& physicsWorld,
                                               Level& level,
                                               Project* project,
                                               const RuntimeLoadOptions& options)
{
    RuntimeLoadReport report;
    const RuntimePaths* runtimePaths = RuntimePaths::try_current();

    if (project && options.seedProjectDefaults)
    {
        if (auto defaultsResult = seed_project_defaults(*project); !defaultsResult)
            report.warnings.push_back(defaultsResult.error().message);
    }

    if (project)
    {
        scriptEngine.set_script_root(project->root_path());
        physicsWorld.set_asset_root(project->root_path());
    }
    else if (runtimePaths)
    {
        scriptEngine.set_script_root(runtimePaths->executable_dir());
        physicsWorld.set_asset_root(std::string{});
    }

    if (runtimePaths)
    {
        const std::filesystem::path defaultVert = runtimePaths->resolve_engine_resource("shader.vert");
        const std::filesystem::path defaultFrag = runtimePaths->resolve_engine_resource("shader.frag");
        renderer.set_shader_paths(defaultVert, defaultFrag);
    }
    else
    {
        renderer.set_shader_paths("Resources/shader.vert", "Resources/shader.frag");
    }

    if (project)
    {
        const std::filesystem::path projectVert = project->get_absolute_path(std::filesystem::path("Assets") / "Shaders" / "shader.vert");
        const std::filesystem::path projectFrag = project->get_absolute_path(std::filesystem::path("Assets") / "Shaders" / "shader.frag");
        if (std::filesystem::exists(projectVert) && std::filesystem::exists(projectFrag))
        {
            const bool projectSupportsGlow = !options.requireGlowShaders ||
                                             (file_contains(projectVert, "inGlow") && file_contains(projectFrag, "fragGlow"));
            if (projectSupportsGlow)
            {
                renderer.set_shader_paths(projectVert, projectFrag);
                report.usedProjectShaders = true;
            }
            else
            {
                report.warnings.push_back(
                    "Project shaders are missing glow support. Using the engine default shaders for this session.");
            }
        }
    }

    renderer.wait_idle();
    auto assetReportResult = reload_level_assets(assetManager, renderer, level.world(), project, options.requireCookedModels);
    if (!assetReportResult)
        return make_error(assetReportResult.error());

    const RuntimeLoadReport& assetReport = assetReportResult.value();
    report.totalMeshes += assetReport.totalMeshes;
    report.totalMaterials += assetReport.totalMaterials;
    report.usedRawSourceAssets = assetReport.usedRawSourceAssets;
    report.warnings.insert(report.warnings.end(), assetReport.warnings.begin(), assetReport.warnings.end());

    RuntimeLoadReport materialReport =
        load_project_material_overrides(assetManager, renderer, level.world(), project,
                                        options.allowEmbeddedMaterialTextureExtraction);
    report.warnings.insert(report.warnings.end(), materialReport.warnings.begin(), materialReport.warnings.end());

    return report;
}

std::vector<std::string> scan_available_scripts(const Project* project)
{
    std::set<std::string> scripts;
    if (!project)
        return {};

    const std::filesystem::path root(project->root_path());
    if (!std::filesystem::exists(root))
        return {};

    std::error_code ec;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua")
            scripts.insert(std::filesystem::relative(entry.path(), root).generic_string());
    }

    return {scripts.begin(), scripts.end()};
}

bool load_material_from_file(const std::filesystem::path& filePath,
                             std::string& outName,
                             MaterialData& outData,
                             const Project* project,
                             bool allowTextureExtraction)
{
    namespace fb = NatureOfCraft::Assets;

    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs)
        return false;

    const auto size = ifs.tellg();
    ifs.seekg(0);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    ifs.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!ifs)
        return false;

    flatbuffers::Verifier verifier(buffer.data(), buffer.size());
    if (!fb::VerifyMaterialAssetBuffer(verifier))
        return false;

    const auto* asset = fb::GetMaterialAsset(buffer.data());
    if (!asset)
        return false;

    outName = asset->name() ? asset->name()->str() : filePath.stem().string();
    if (const auto* color = asset->albedo_color())
        outData.albedoColor = {color->r(), color->g(), color->b(), color->a()};
    outData.roughness = asset->roughness();
    outData.metallic = asset->metallic();

    std::filesystem::path textureDirectory;
    if (project)
        textureDirectory = std::filesystem::path(project->root_path()) / project->asset_directory() / "Textures";

    auto extractTexture = [&](const ::flatbuffers::String* pathField,
                              const ::flatbuffers::Vector<std::uint8_t>* dataField,
                              std::filesystem::path& outPath) {
        if (!pathField || pathField->size() == 0)
            return;

        const std::string filename = pathField->str();
        outPath = std::filesystem::path(project ? project->asset_directory() : "Assets") / "Textures" / filename;

        if (!allowTextureExtraction || !project || !dataField || dataField->size() == 0)
            return;

        const std::filesystem::path destinationFile = textureDirectory / filename;
        if (std::filesystem::exists(destinationFile))
            return;

        std::error_code ec;
        std::filesystem::create_directories(textureDirectory, ec);
        std::ofstream ofs(destinationFile, std::ios::binary);
        if (ofs)
            ofs.write(reinterpret_cast<const char*>(dataField->data()), static_cast<std::streamsize>(dataField->size()));
    };

    extractTexture(asset->albedo_texture_path(), asset->albedo_texture_data(), outData.albedoTexturePath);
    extractTexture(asset->normal_texture_path(), asset->normal_texture_data(), outData.normalTexturePath);
    extractTexture(asset->roughness_texture_path(), asset->roughness_texture_data(), outData.roughnessTexturePath);
    extractTexture(asset->metallic_texture_path(), asset->metallic_texture_data(), outData.metallicTexturePath);
    extractTexture(asset->ao_texture_path(), asset->ao_texture_data(), outData.aoTexturePath);
    return true;
}

std::vector<std::filesystem::path> scan_material_files(const Project* project)
{
    std::vector<std::filesystem::path> result;
    if (!project)
        return result;

    const std::filesystem::path root(project->root_path());
    if (!std::filesystem::exists(root))
        return result;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".noc_material")
            result.push_back(entry.path());
    }

    return result;
}

Result<CookProjectResult> cook_project(const std::filesystem::path& projectFilePath, const CookProjectOptions& options)
{
    if (options.outputRoot.empty())
        return make_error("Cook output root is required", ErrorCode::AssetCacheWriteFailed);

    auto projectResult = Project::load(projectFilePath.string());
    if (!projectResult)
        return make_error(projectResult.error());

    Project project = std::move(projectResult.value());
    CookProjectResult result;

    std::error_code ec;
    if (options.overwriteOutput)
        std::filesystem::remove_all(options.outputRoot, ec);

    std::filesystem::create_directories(options.outputRoot, ec);
    if (ec)
        return make_error(fmt::format("Failed to create cook output '{}': {}", options.outputRoot.string(), ec.message()),
                          ErrorCode::AssetCacheWriteFailed);

    const std::filesystem::path gameOutputRoot = options.outputRoot / "Game";
    const std::filesystem::path engineOutputRoot = options.outputRoot / "Engine";

    ec.clear();
    std::filesystem::create_directories(gameOutputRoot, ec);
    if (ec)
        return make_error(fmt::format("Failed to create cooked game output '{}': {}", gameOutputRoot.string(), ec.message()),
                          ErrorCode::AssetCacheWriteFailed);

    const RuntimePaths* runtimePaths = RuntimePaths::try_current();
    if (runtimePaths)
    {
        if (auto copyEngineResult = copy_engine_runtime_resources(*runtimePaths, engineOutputRoot, options.compileShaders, result);
            !copyEngineResult)
        {
            return make_error(copyEngineResult.error());
        }
    }

    if (auto copyProjectResult =
            copy_file_if_needed(projectFilePath, gameOutputRoot / projectFilePath.filename(), true);
        !copyProjectResult)
    {
        return make_error(copyProjectResult.error());
    }
    result.cookedProjectFile = gameOutputRoot / projectFilePath.filename();

    const std::filesystem::path sourceShadersDir = std::filesystem::path(project.root_path()) / "Assets" / "Shaders";
    const std::filesystem::path sourceProjectVert = sourceShadersDir / "shader.vert";
    const std::filesystem::path sourceProjectFrag = sourceShadersDir / "shader.frag";
    if (std::filesystem::exists(sourceProjectVert) && std::filesystem::exists(sourceProjectFrag))
    {
        const std::filesystem::path outputShaderDir = gameOutputRoot / "Assets" / "Shaders";
        std::filesystem::create_directories(outputShaderDir, ec);
        if (ec)
        {
            return make_error(fmt::format("Failed to create shader output '{}': {}", outputShaderDir.string(), ec.message()),
                              ErrorCode::AssetCacheWriteFailed);
        }

        for (const auto& shaderName : {"shader.vert", "shader.frag"})
        {
            const std::filesystem::path sourcePath = sourceShadersDir / shaderName;
            if (auto copyShaderResult = copy_file_if_needed(sourcePath, outputShaderDir / shaderName, true); !copyShaderResult)
                return make_error(copyShaderResult.error());
        }

        if (options.compileShaders)
        {
            const std::filesystem::path cookedVert = outputShaderDir / "shader.vert";
            const std::filesystem::path cookedFrag = outputShaderDir / "shader.frag";
            if (auto vertCompile = ShaderCompiler::compile_to_file(cookedVert, ShaderCompiler::get_spv_path(cookedVert));
                !vertCompile)
            {
                return make_error(vertCompile.error());
            }
            if (auto fragCompile = ShaderCompiler::compile_to_file(cookedFrag, ShaderCompiler::get_spv_path(cookedFrag));
                !fragCompile)
            {
                return make_error(fragCompile.error());
            }
        }
    }
    else if (std::filesystem::exists(sourceProjectVert) != std::filesystem::exists(sourceProjectFrag))
    {
        const std::string warning =
            "Project shader override is incomplete. Shipping bundle will use the engine default shaders instead.";
        if (options.strict)
            return make_error(warning, ErrorCode::AssetInvalidData);

        result.warnings.push_back(warning);
    }

    const auto materialLookup = build_project_material_lookup(project, result);
    std::set<std::string> copiedScripts;
    std::set<std::string> copiedMaterials;
    std::unordered_map<std::string, std::string> cookedAssetMap;
    std::size_t uniqueModelIndex = 0;

    for (const auto& levelEntry : project.levels())
    {
        auto levelResult = Level::load(project.get_absolute_path(levelEntry.filePath));
        if (!levelResult)
            return make_error(levelResult.error());

        Level level = std::move(levelResult.value());

        for (const auto& scriptPath : collect_referenced_script_paths(level.world()))
        {
            if (!copiedScripts.insert(scriptPath).second)
                continue;

            auto scriptCopyResult = copy_project_relative_file_for_cook(resolve_asset_path(scriptPath, &project), project.root_path(),
                                                                        gameOutputRoot, result.copiedScriptCount);
            if (!scriptCopyResult)
            {
                if (options.strict)
                    return make_error(scriptCopyResult.error());

                result.warnings.push_back(scriptCopyResult.error().message);
            }
        }

        for (const auto& materialName : collect_referenced_material_names(level.world()))
        {
            if (!copiedMaterials.insert(materialName).second)
                continue;

            auto materialCopyResult =
                copy_referenced_material_asset(materialName, project, materialLookup, gameOutputRoot, result);
            if (!materialCopyResult)
            {
                if (options.strict)
                    return make_error(materialCopyResult.error());

                result.warnings.push_back(materialCopyResult.error().message);
            }
        }

        auto& registry = level.world().registry();
        auto view = registry.view<MeshComponent>();
        for (auto entity : view)
        {
            auto& meshComponent = view.get<MeshComponent>(entity);
            if (meshComponent.assetPath.empty())
                continue;

            const std::string originalAssetKey = meshComponent.assetPath;
            if (const auto cookedIt = cookedAssetMap.find(originalAssetKey); cookedIt != cookedAssetMap.end())
            {
                meshComponent.assetPath = cookedIt->second;
                continue;
            }

            const std::filesystem::path sourceAssetPath = resolve_asset_path(meshComponent.assetPath, &project);
            if (sourceAssetPath.extension() == ".noc_model")
            {
                std::filesystem::path cookedRelativePath = meshComponent.assetPath;
                if (cookedRelativePath.is_absolute())
                    cookedRelativePath = make_unique_cooked_model_path(gameOutputRoot, sourceAssetPath.stem().string(), uniqueModelIndex++);
                if (auto copyModelResult = copy_file_if_needed(sourceAssetPath, gameOutputRoot / cookedRelativePath, true);
                    !copyModelResult)
                {
                    return make_error(copyModelResult.error());
                }

                auto cachedModelResult = ModelLoader::read_cache(sourceAssetPath);
                if (cachedModelResult)
                {
                    for (const auto& texturePath : collect_model_texture_load_paths(*cachedModelResult.value(), &project))
                    {
                        auto textureCopyResult = copy_texture_for_cook(texturePath, gameOutputRoot, result);
                        if (!textureCopyResult && options.strict)
                            return make_error(textureCopyResult.error());
                    }
                }

                meshComponent.assetPath = cookedRelativePath.generic_string();
                cookedAssetMap.emplace(originalAssetKey, meshComponent.assetPath);
                continue;
            }

            auto parsedModelResult = ModelLoader::parse_model(sourceAssetPath);
            if (!parsedModelResult)
                return make_error(parsedModelResult.error());

            ModelData cookedModel = *parsedModelResult.value();
            for (auto& material : cookedModel.materials)
            {
                for (auto* texturePath :
                     {&material.albedoTexturePath, &material.normalTexturePath, &material.roughnessTexturePath,
                      &material.metallicTexturePath, &material.aoTexturePath})
                {
                    if (texturePath->empty())
                        continue;

                    auto textureCopyResult = copy_texture_for_cook(*texturePath, gameOutputRoot, result);
                    if (!textureCopyResult)
                    {
                        if (options.strict)
                            return make_error(textureCopyResult.error());

                        result.warnings.push_back(textureCopyResult.error().message);
                        texturePath->clear();
                        continue;
                    }

                    *texturePath = textureCopyResult.value();
                }
            }

            const std::string preferredName = cookedModel.name.empty() ? sourceAssetPath.stem().string() : cookedModel.name;
            const std::filesystem::path cookedRelativePath =
                make_unique_cooked_model_path(gameOutputRoot, preferredName, uniqueModelIndex++);
            if (auto writeResult = ModelLoader::write_cache(cookedModel, gameOutputRoot / cookedRelativePath); !writeResult)
                return make_error(writeResult.error());

            meshComponent.assetPath = cookedRelativePath.generic_string();
            cookedAssetMap.emplace(originalAssetKey, meshComponent.assetPath);
            ++result.cookedModelCount;
        }

        const std::filesystem::path cookedLevelPath = gameOutputRoot / levelEntry.filePath;
        if (auto saveResult = level.save_as(cookedLevelPath.string()); !saveResult)
            return make_error(saveResult.error());

        ++result.cookedLevelCount;
    }

    return result;
}

Result<BundleProjectResult> bundle_project(const std::filesystem::path& projectFilePath, const BundleProjectOptions& options)
{
    if (options.outputRoot.empty())
        return make_error("Bundle output root is required", ErrorCode::AssetCacheWriteFailed);

    std::error_code ec;
    const std::filesystem::path bundleRoot = std::filesystem::absolute(options.outputRoot, ec);
    std::filesystem::path resolvedRuntimeRoot;
    if (options.runtimeBinaryRoot.empty())
    {
        auto runtimeRootResult = detect_runtime_binary_root();
        if (!runtimeRootResult)
            return make_error(runtimeRootResult.error());

        resolvedRuntimeRoot = runtimeRootResult.value();
    }
    else
    {
        ec.clear();
        resolvedRuntimeRoot = std::filesystem::absolute(options.runtimeBinaryRoot, ec);
        if (ec)
        {
            return make_error(fmt::format("Failed to resolve runtime directory '{}': {}", options.runtimeBinaryRoot.string(),
                                          ec.message()),
                              ErrorCode::AssetFileNotFound);
        }
    }

    if (resolvedRuntimeRoot.empty())
        return make_error("Runtime binary root is required", ErrorCode::AssetFileNotFound);
    if (bundleRoot == resolvedRuntimeRoot)
        return make_error("Bundle output root must be different from the runtime binary directory",
                          ErrorCode::AssetCacheWriteFailed);
    if (!std::filesystem::exists(resolvedRuntimeRoot / "Game.exe"))
    {
        return make_error(fmt::format("Game.exe was not found in runtime directory '{}'", resolvedRuntimeRoot.string()),
                          ErrorCode::AssetFileNotFound);
    }

    BundleProjectResult result;
    result.bundleRoot = bundleRoot;
    result.contentRoot = bundleRoot / "Content";

    if (options.overwriteOutput)
        std::filesystem::remove_all(bundleRoot, ec);

    ec.clear();
    std::filesystem::create_directories(bundleRoot, ec);
    if (ec)
    {
        return make_error(fmt::format("Failed to create bundle output '{}': {}", bundleRoot.string(), ec.message()),
                          ErrorCode::AssetCacheWriteFailed);
    }

    CookProjectOptions cookOptions;
    cookOptions.outputRoot = result.contentRoot;
    cookOptions.overwriteOutput = true;
    cookOptions.compileShaders = options.compileShaders;
    cookOptions.strict = options.strict;
    auto cookResult = cook_project(projectFilePath, cookOptions);
    if (!cookResult)
        return make_error(cookResult.error());

    result.cookResult = std::move(cookResult.value());

    for (const auto& entry : std::filesystem::directory_iterator(resolvedRuntimeRoot))
    {
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path sourcePath = entry.path();
        const std::string extension = to_lower_copy(sourcePath.extension().string());
        const bool isRuntimePayload = sourcePath.filename() == "Game.exe" || extension == ".dll";
        if (!isRuntimePayload)
            continue;

        const std::filesystem::path destinationPath = bundleRoot / sourcePath.filename();
        const bool destinationExists = std::filesystem::exists(destinationPath);
        if (auto copyResult = copy_file_if_needed(sourcePath, destinationPath, true); !copyResult)
            return make_error(copyResult.error());

        if (!destinationExists)
            ++result.copiedRuntimeFileCount;
    }

    result.warnings = result.cookResult.warnings;
    return result;
}
