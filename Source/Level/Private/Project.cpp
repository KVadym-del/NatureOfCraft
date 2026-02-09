#include "../Public/Project.hpp"

#include <ProjectAsset_generated.h>

#include <flatbuffers/flatbuffers.h>
#include <fmt/core.h>

#include <filesystem>
#include <fstream>

namespace fb = flatbuffers;
namespace fbp = NatureOfCraft::Project;
namespace fs = std::filesystem;

// ── Factories ────────────────────────────────────────────────────────

Result<Project> Project::create_new(std::string name, std::string rootPath)
{
    // Ensure the root directory exists (create it if not).
    std::error_code ec;
    fs::create_directories(rootPath, ec);
    if (ec)
        return make_error(fmt::format("Failed to create project directory '{}': {}", rootPath, ec.message()),
                          ErrorCode::AssetCacheWriteFailed);

    Project project;
    project.m_name = std::move(name);
    project.m_rootPath = std::move(rootPath);
    project.m_assetDirectory = "Assets";

    // Create the asset sub-directory.
    fs::create_directories(fs::path(project.m_rootPath) / project.m_assetDirectory, ec);
    // Non-fatal if asset dir creation fails; user can create it manually.

    // Save the initial manifest.
    auto result = project.save();
    if (!result)
        return make_error(result.error());

    return project;
}

Result<Project> Project::load(const std::string& projectFilePath)
{
    std::ifstream file(projectFilePath, std::ios::binary | std::ios::ate);
    if (!file)
        return make_error(fmt::format("Failed to open project file: {}", projectFilePath),
                          ErrorCode::AssetFileNotFound);

    auto size = file.tellg();
    if (size <= 0)
        return make_error(fmt::format("Project file is empty: {}", projectFilePath), ErrorCode::AssetParsingFailed);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file)
        return make_error(fmt::format("Failed to read project file: {}", projectFilePath),
                          ErrorCode::AssetCacheReadFailed);

    // Verify buffer
    fb::Verifier verifier(buffer.data(), buffer.size());
    if (!fbp::VerifyProjectAssetBuffer(verifier))
        return make_error("Invalid or corrupted project file", ErrorCode::AssetParsingFailed);

    const auto* asset = fbp::GetProjectAsset(buffer.data());
    if (!asset)
        return make_error("Failed to parse project asset", ErrorCode::AssetParsingFailed);

    Project project;
    project.m_name = asset->name() ? asset->name()->str() : "";
    project.m_assetDirectory = asset->asset_directory() ? asset->asset_directory()->str() : "Assets";

    // Derive root path from the project file location.
    fs::path filePath(projectFilePath);
    project.m_rootPath = filePath.parent_path().string();

    // Load level entries.
    if (const auto* levels = asset->levels())
    {
        project.m_levels.reserve(levels->size());
        for (const auto* entry : *levels)
        {
            LevelEntry le;
            le.name = entry->name() ? entry->name()->str() : "";
            le.filePath = entry->file_path() ? entry->file_path()->str() : "";
            project.m_levels.push_back(std::move(le));
        }
    }

    return project;
}

// ── Persistence ──────────────────────────────────────────────────────

Result<> Project::save()
{
    fb::FlatBufferBuilder fbb(1024);

    // Build level entries.
    std::vector<fb::Offset<fbp::LevelEntry>> levelOffsets;
    levelOffsets.reserve(m_levels.size());
    for (const auto& le : m_levels)
    {
        levelOffsets.push_back(fbp::CreateLevelEntryDirect(fbb, le.name.c_str(), le.filePath.c_str()));
    }

    auto asset = fbp::CreateProjectAssetDirect(fbb, m_name.c_str(),
                                               1, // version
                                               &levelOffsets, m_assetDirectory.c_str());

    fbp::FinishProjectAssetBuffer(fbb, asset);

    // Write to file.
    std::string outPath = get_project_file_path();
    std::ofstream file(outPath, std::ios::binary);
    if (!file)
        return make_error(fmt::format("Failed to open project file for writing: {}", outPath),
                          ErrorCode::AssetCacheWriteFailed);

    file.write(reinterpret_cast<const char*>(fbb.GetBufferPointer()), static_cast<std::streamsize>(fbb.GetSize()));
    if (!file)
        return make_error(fmt::format("Failed to write project file: {}", outPath), ErrorCode::AssetCacheWriteFailed);

    return {};
}

// ── Level management ─────────────────────────────────────────────────

void Project::add_level(std::string name, std::string relativePath)
{
    m_levels.push_back(LevelEntry{std::move(name), std::move(relativePath)});
}

void Project::remove_level(size_t index)
{
    if (index < m_levels.size())
        m_levels.erase(m_levels.begin() + static_cast<ptrdiff_t>(index));
}

// ── Accessors ────────────────────────────────────────────────────────

std::string Project::get_absolute_path(const std::filesystem::path& relativePath) const
{
    return (fs::path(m_rootPath) / relativePath).string();
}

std::string Project::get_project_file_path() const
{
    return (fs::path(m_rootPath) / (m_name + ".noc_project")).string();
}
