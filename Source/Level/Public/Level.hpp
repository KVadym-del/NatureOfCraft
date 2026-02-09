#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"
#include "../../ECS/Public/World.hpp"

#include <string>

NOC_SUPPRESS_DLL_WARNINGS

/// A Level owns a World (ECS registry) plus metadata (name, file path, dirty flag).
/// Provides static factories for creating new levels and loading from .noc_level files.
class NOC_EXPORT Level
{
  public:
    Level() = default;

    // ── Factories ──────────────────────────────────────────────────────

    /// Creates a new empty level with a default active camera entity.
    static Level create_new(std::string name);

    /// Loads a level from a .noc_level binary file.
    static Result<Level> load(const std::string& filePath);

    // ── Persistence ────────────────────────────────────────────────────

    /// Saves the level to its current file path. Fails if no path is set.
    Result<> save();

    /// Saves the level to the given file path and updates the stored path.
    Result<> save_as(const std::string& filePath);

    // ── Accessors ──────────────────────────────────────────────────────

    World& world() noexcept
    {
        return m_world;
    }
    const World& world() const noexcept
    {
        return m_world;
    }

    const std::string& name() const noexcept
    {
        return m_name;
    }
    void set_name(const std::string& name)
    {
        m_name = name;
    }

    const std::string& file_path() const noexcept
    {
        return m_filePath;
    }

    bool is_dirty() const noexcept
    {
        return m_dirty;
    }
    void mark_dirty() noexcept
    {
        m_dirty = true;
    }

  private:
    World m_world;
    std::string m_name;
    std::string m_filePath;
    bool m_dirty{false};
};

NOC_RESTORE_DLL_WARNINGS
