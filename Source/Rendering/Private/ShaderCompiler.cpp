#include "../Public/ShaderCompiler.hpp"

#include <filesystem>
#include <fstream>
#include <regex>

#include <fmt/core.h>
#include <shaderc/shaderc.hpp>

// ── Public API ───────────────────────────────────────────────────────

Result<std::vector<std::uint32_t>> ShaderCompiler::compile(
    std::string_view glslSource,
    ShaderStage stage,
    std::string_view filename
)
{
    shaderc::Compiler compiler{};
    shaderc::CompileOptions options{};

    // Target Vulkan 1.0 / SPIR-V 1.0 for maximum compatibility
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc_shader_kind kind{};
    switch (stage)
    {
    case ShaderStage::Vertex:
        kind = shaderc_glsl_vertex_shader;
        break;
    case ShaderStage::Fragment:
        kind = shaderc_glsl_fragment_shader;
        break;
    case ShaderStage::Compute:
        kind = shaderc_glsl_compute_shader;
        break;
    }

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        glslSource.data(),
        glslSource.size(),
        kind,
        filename.data(),
        options
    );

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        return make_error(
            fmt::format(
                "Shader compilation failed ({}): {}",
                filename,
                result.GetErrorMessage()
            ),
            ErrorCode::ShaderCompilationFailed
        );
    }

    std::vector<std::uint32_t> spirv(result.cbegin(), result.cend());
    return spirv;
}

Result<std::vector<std::uint32_t>> ShaderCompiler::compile_file(const std::filesystem::path& glslPath)
{
    if (!std::filesystem::exists(glslPath))
        return make_error(
            fmt::format("Shader source file not found: {}", glslPath.string()),
            ErrorCode::AssetFileNotFound
        );

    // Detect shader stage from extension
    auto stageResult = detect_stage(glslPath);
    if (!stageResult)
        return make_error(stageResult.error());

    // Read source
    std::ifstream file(glslPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(
            fmt::format("Failed to open shader source: {}", glslPath.string()),
            ErrorCode::FileReadFailed
        );

    auto size = file.tellg();
    file.seekg(0);
    std::string source(static_cast<std::size_t>(size), '\0');
    file.read(source.data(), size);

    return compile(source, stageResult.value(), glslPath.filename().string());
}

Result<std::vector<std::uint32_t>> ShaderCompiler::compile_or_cache(const std::filesystem::path& glslPath, const std::filesystem::path& spvPath)
{
    // Check if cached .spv exists and is newer than the source
    if (std::filesystem::exists(spvPath) && std::filesystem::exists(glslPath))
    {
        auto srcTime = std::filesystem::last_write_time(glslPath);
        auto spvTime = std::filesystem::last_write_time(spvPath);

        if (spvTime >= srcTime)
        {
            fmt::print("Using cached shader: {}\n", spvPath.string());
            return read_spv(spvPath);
        }
    }

    // Compile from source
    fmt::print("Compiling shader: {} -> {}\n", glslPath.string(), spvPath.string());
    auto spirvResult = compile_file(glslPath);
    if (!spirvResult)
        return spirvResult;

    // Cache the result
    std::filesystem::create_directories(spvPath.parent_path());
    auto writeResult = write_spv(spvPath, spirvResult.value());
    if (!writeResult)
        fmt::print("Warning: failed to cache shader: {}\n", writeResult.error().message);

    return spirvResult;
}

std::filesystem::path ShaderCompiler::get_spv_path(const std::filesystem::path& glslPath)
{
    std::filesystem::path result{glslPath};
    result += ".spv"; // e.g. shader.vert -> shader.vert.spv
    return result;
}

// ── Private helpers ──────────────────────────────────────────────────

Result<ShaderStage> ShaderCompiler::detect_stage(const std::filesystem::path& glslPath)
{
    auto ext = glslPath.extension().string();
    if (ext == ".vert")
        return ShaderStage::Vertex;
    if (ext == ".frag")
        return ShaderStage::Fragment;
    if (ext == ".comp" || ext == ".glsl")
        return ShaderStage::Compute;

    return make_error(
        fmt::format("Unknown shader extension '{}' — expected .vert, .frag, .comp, or .glsl", ext),
        ErrorCode::ShaderCompilationFailed
    );
}

Result<std::vector<std::uint32_t>> ShaderCompiler::read_spv(const std::filesystem::path& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(
            fmt::format("Failed to open cached shader: {}", spvPath.string()),
            ErrorCode::FileReadFailed
        );

    auto fileSize = static_cast<std::size_t>(file.tellg());
    if (fileSize == 0 || fileSize % sizeof(std::uint32_t) != 0)
        return make_error(
            fmt::format("Invalid SPIR-V file (size {}): {}", fileSize, spvPath.string()),
            ErrorCode::ShaderCompilationFailed
        );

    file.seekg(0);
    std::vector<std::uint32_t> spirv(fileSize / sizeof(std::uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(fileSize));

    return spirv;
}

Result<> ShaderCompiler::write_spv(const std::filesystem::path& spvPath, const std::vector<std::uint32_t>& spirv)
{
    std::ofstream file(spvPath, std::ios::binary);
    if (!file.is_open())
        return make_error(
            fmt::format("Failed to open shader cache for writing: {}", spvPath.string()),
            ErrorCode::AssetCacheWriteFailed
        );

    file.write(
        reinterpret_cast<const char*>(spirv.data()),
        static_cast<std::streamsize>(spirv.size() * sizeof(std::uint32_t))
    );
    if (!file.good())
        return make_error(
            fmt::format("Failed to write shader cache: {}", spvPath.string()),
            ErrorCode::AssetCacheWriteFailed
        );

    return {};
}

Result<std::vector<std::uint32_t>> ShaderCompiler::compile_compute_with_includes(
    const std::filesystem::path& glslPath,
    const std::vector<std::filesystem::path>& includeDirs)
{
    if (!std::filesystem::exists(glslPath))
        return make_error(
            fmt::format("Compute shader source not found: {}", glslPath.string()),
            ErrorCode::AssetFileNotFound
        );

    // Read the main shader source
    std::ifstream mainFile(glslPath, std::ios::binary | std::ios::ate);
    if (!mainFile.is_open())
        return make_error(
            fmt::format("Failed to open compute shader: {}", glslPath.string()),
            ErrorCode::FileReadFailed
        );

    auto mainSize = mainFile.tellg();
    mainFile.seekg(0);
    std::string source(static_cast<std::size_t>(mainSize), '\0');
    mainFile.read(source.data(), mainSize);
    mainFile.close();

    // Resolve #include directives by inlining file contents.
    // Matches: #include "filename.h"
    std::regex includeRegex("^\\s*#include\\s+\"([^\"]+)\"", std::regex::multiline);
    std::string resolved{};
    std::sregex_iterator it(source.begin(), source.end(), includeRegex);
    std::sregex_iterator end{};
    std::size_t lastPos{};

    for (; it != end; ++it)
    {
        const auto& match = *it;
        resolved.append(source, lastPos, static_cast<std::size_t>(match.position()) - lastPos);

        std::string includeName = match[1].str();
        bool found{false};

        for (const auto& dir : includeDirs)
        {
            auto includePath = dir / includeName;
            if (std::filesystem::exists(includePath))
            {
                std::ifstream incFile(includePath, std::ios::binary | std::ios::ate);
                if (incFile.is_open())
                {
                    auto incSize = incFile.tellg();
                    incFile.seekg(0);
                    std::string incContent(static_cast<std::size_t>(incSize), '\0');
                    incFile.read(incContent.data(), incSize);
                    resolved += incContent;
                    resolved += '\n';
                    found = true;
                    break;
                }
            }
        }

        if (!found)
        {
            return make_error(
                fmt::format("Could not resolve #include \"{}\" from {}", includeName, glslPath.string()),
                ErrorCode::ShaderCompilationFailed
            );
        }

        lastPos = static_cast<std::size_t>(match.position()) + match.length();
    }

    resolved.append(source, lastPos, source.size() - lastPos);

    return compile(resolved, ShaderStage::Compute, glslPath.filename().string());
}
