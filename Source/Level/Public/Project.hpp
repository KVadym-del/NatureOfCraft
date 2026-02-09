#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>

NOC_SUPPRESS_DLL_WARNINGS

/// A reference to a single level within a project.
struct LevelEntry
{
    std::string name;
    std::string filePath; // relative path to .noc_level file from project root
};

/// Project manifest: manages a collection of levels and a shared asset directory.
/// Persisted as a .noc_project FlatBuffers binary at the project root.
class NOC_EXPORT Project
{
  public:
    Project() = default;

    // ── Factories ──────────────────────────────────────────────────────

    /// Creates a new empty project at the given root path.
    /// Also creates the project directory and asset sub-directory on disk if they don't exist.
    static Result<Project> create_new(std::string name, std::string rootPath);

    /// Loads a project from a .noc_project file.
    static Result<Project> load(const std::string& projectFilePath);

    // ── Persistence ────────────────────────────────────────────────────

    /// Saves the project manifest to its .noc_project file.
    Result<> save();

    // ── Level management ───────────────────────────────────────────────

    /// Adds a level entry to the project.
    void add_level(std::string name, std::string relativePath);

    /// Removes a level entry by index.
    void remove_level(size_t index);

    /// Returns all level entries.
    const std::vector<LevelEntry>& levels() const noexcept
    {
        return m_levels;
    }

    // ── Accessors ──────────────────────────────────────────────────────

    const std::string& name() const noexcept
    {
        return m_name;
    }
    void set_name(const std::string& name)
    {
        m_name = name;
    }

    const std::string& root_path() const noexcept
    {
        return m_rootPath;
    }

    const std::string& asset_directory() const noexcept
    {
        return m_assetDirectory;
    }
    void set_asset_directory(const std::string& dir)
    {
        m_assetDirectory = dir;
    }

    /// Resolves a relative path to an absolute path using the project root.
    std::string get_absolute_path(const std::filesystem::path& relativePath) const;

    /// Returns the full path to the .noc_project manifest file.
    std::string get_project_file_path() const;

  private:
    std::string m_name;
    std::string m_rootPath;
    std::string m_assetDirectory{"Assets"};
    std::vector<LevelEntry> m_levels;
};

NOC_RESTORE_DLL_WARNINGS
