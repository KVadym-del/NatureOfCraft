#include "../Public/RuntimePaths.hpp"

#include <cstdlib>
#include <optional>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
std::optional<RuntimePaths>& runtime_paths_storage()
{
    static std::optional<RuntimePaths> paths;
    return paths;
}

std::filesystem::path normalize_existing_path(const std::filesystem::path& path)
{
    if (path.empty())
        return {};

    std::error_code ec;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    if (ec)
        return path;

    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, ec);
    if (ec)
        return absolutePath;

    return canonicalPath;
}

Result<std::filesystem::path> discover_executable_path(const std::filesystem::path& executableHint)
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size())
    {
        buffer.resize(size);
        return normalize_existing_path(std::filesystem::path(buffer));
    }
#endif

    if (!executableHint.empty())
        return normalize_existing_path(executableHint);

    return make_error("Failed to determine executable path", ErrorCode::FileReadFailed);
}

std::filesystem::path discover_user_data_root(std::string_view appName)
{
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && *localAppData)
        return std::filesystem::path(localAppData) / appName;
#endif

    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local" / "share" / appName;

    std::error_code ec;
    return std::filesystem::absolute(std::filesystem::path(appName) / "UserData", ec);
}
} // namespace

Result<> RuntimePaths::initialize_current_process(std::string_view appName,
                                                  const std::filesystem::path& executableHint,
                                                  const std::filesystem::path& contentRootOverride,
                                                  const std::filesystem::path& userDataRootOverride)
{
    auto executablePathResult = discover_executable_path(executableHint);
    if (!executablePathResult)
        return make_error(executablePathResult.error());

    RuntimePaths paths;
    paths.m_appName = std::string(appName);
    paths.m_executablePath = executablePathResult.value();
    paths.m_executableDir = paths.m_executablePath.parent_path();
    paths.m_contentRoot =
        contentRootOverride.empty() ? normalize_existing_path(paths.m_executableDir / "Content")
                                    : normalize_existing_path(contentRootOverride);
    paths.m_engineResourcesDir = paths.m_contentRoot / "Engine";
    paths.m_gameContentDir = paths.m_contentRoot / "Game";
    paths.m_legacyResourcesDir = paths.m_executableDir / "Resources";
    paths.m_userDataRoot =
        userDataRootOverride.empty() ? normalize_existing_path(discover_user_data_root(appName))
                                     : normalize_existing_path(userDataRootOverride);
    paths.m_shaderCacheDir = paths.m_userDataRoot / "ShaderCache";
    paths.m_logsDir = paths.m_userDataRoot / "Logs";

    runtime_paths_storage() = std::move(paths);
    return runtime_paths_storage()->ensure_user_directories();
}

const RuntimePaths* RuntimePaths::try_current() noexcept
{
    const auto& storage = runtime_paths_storage();
    return storage ? &*storage : nullptr;
}

const RuntimePaths& RuntimePaths::current()
{
    const RuntimePaths* paths = try_current();
    if (!paths)
        throw std::runtime_error("RuntimePaths is not initialized");
    return *paths;
}

std::filesystem::path RuntimePaths::resolve_engine_resource(const std::filesystem::path& relativePath) const
{
    const std::filesystem::path modernPath = m_engineResourcesDir / relativePath;
    std::error_code ec;
    if (std::filesystem::exists(modernPath, ec))
        return modernPath;

    const std::filesystem::path legacyPath = m_legacyResourcesDir / relativePath;
    ec.clear();
    if (std::filesystem::exists(legacyPath, ec))
        return legacyPath;

    if (std::filesystem::exists(m_engineResourcesDir, ec))
        return modernPath;

    return legacyPath;
}

std::filesystem::path RuntimePaths::resolve_game_content(const std::filesystem::path& relativePath) const
{
    return m_gameContentDir / relativePath;
}

std::filesystem::path RuntimePaths::resolve_user_data(const std::filesystem::path& relativePath) const
{
    return m_userDataRoot / relativePath;
}

Result<> RuntimePaths::ensure_user_directories() const
{
    std::error_code ec;
    std::filesystem::create_directories(m_userDataRoot, ec);
    if (ec)
        return make_error("Failed to create user data directory", ErrorCode::AssetCacheWriteFailed);

    ec.clear();
    std::filesystem::create_directories(m_shaderCacheDir, ec);
    if (ec)
        return make_error("Failed to create shader cache directory", ErrorCode::AssetCacheWriteFailed);

    ec.clear();
    std::filesystem::create_directories(m_logsDir, ec);
    if (ec)
        return make_error("Failed to create log directory", ErrorCode::AssetCacheWriteFailed);

    return {};
}
