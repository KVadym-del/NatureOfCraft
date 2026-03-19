#pragma once
#include "Core.hpp"
#include "Expected.hpp"

#include <filesystem>
#include <string>
#include <string_view>

NOC_SUPPRESS_DLL_WARNINGS

class NOC_EXPORT RuntimePaths
{
  public:
    static Result<> initialize_current_process(std::string_view appName,
                                               const std::filesystem::path& executableHint = {},
                                               const std::filesystem::path& contentRootOverride = {},
                                               const std::filesystem::path& userDataRootOverride = {});

    static const RuntimePaths* try_current() noexcept;
    static const RuntimePaths& current();

    const std::string& app_name() const noexcept
    {
        return m_appName;
    }

    const std::filesystem::path& executable_path() const noexcept
    {
        return m_executablePath;
    }

    const std::filesystem::path& executable_dir() const noexcept
    {
        return m_executableDir;
    }

    const std::filesystem::path& content_root() const noexcept
    {
        return m_contentRoot;
    }

    const std::filesystem::path& engine_resources_dir() const noexcept
    {
        return m_engineResourcesDir;
    }

    const std::filesystem::path& game_content_dir() const noexcept
    {
        return m_gameContentDir;
    }

    const std::filesystem::path& legacy_resources_dir() const noexcept
    {
        return m_legacyResourcesDir;
    }

    const std::filesystem::path& user_data_root() const noexcept
    {
        return m_userDataRoot;
    }

    const std::filesystem::path& shader_cache_dir() const noexcept
    {
        return m_shaderCacheDir;
    }

    const std::filesystem::path& logs_dir() const noexcept
    {
        return m_logsDir;
    }

    std::filesystem::path resolve_engine_resource(const std::filesystem::path& relativePath) const;
    std::filesystem::path resolve_game_content(const std::filesystem::path& relativePath) const;
    std::filesystem::path resolve_user_data(const std::filesystem::path& relativePath) const;

    Result<> ensure_user_directories() const;

  private:
    std::string m_appName;
    std::filesystem::path m_executablePath;
    std::filesystem::path m_executableDir;
    std::filesystem::path m_contentRoot;
    std::filesystem::path m_engineResourcesDir;
    std::filesystem::path m_gameContentDir;
    std::filesystem::path m_legacyResourcesDir;
    std::filesystem::path m_userDataRoot;
    std::filesystem::path m_shaderCacheDir;
    std::filesystem::path m_logsDir;
};

NOC_RESTORE_DLL_WARNINGS
