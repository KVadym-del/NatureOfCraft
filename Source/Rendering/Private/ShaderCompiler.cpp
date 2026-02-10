#include "../Public/ShaderCompiler.hpp"

#include <filesystem>
#include <fstream>

#include <fmt/core.h>
#include <shaderc/shaderc.hpp>

namespace fs = std::filesystem;

// ── Public API ───────────────────────────────────────────────────────

Result<std::vector<uint32_t>> ShaderCompiler::compile(std::string_view glslSource, ShaderStage stage,
                                                      std::string_view filename)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Target Vulkan 1.0 / SPIR-V 1.0 for maximum compatibility
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    shaderc_shader_kind kind =
        (stage == ShaderStage::Vertex) ? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader;

    shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(glslSource.data(), glslSource.size(), kind, filename.data(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        return make_error(fmt::format("Shader compilation failed ({}): {}", filename, result.GetErrorMessage()),
                          ErrorCode::ShaderCompilationFailed);
    }

    std::vector<uint32_t> spirv(result.cbegin(), result.cend());
    return spirv;
}

Result<std::vector<uint32_t>> ShaderCompiler::compile_file(const fs::path& glslPath)
{
    if (!fs::exists(glslPath))
        return make_error(fmt::format("Shader source file not found: {}", glslPath.string()),
                          ErrorCode::AssetFileNotFound);

    // Detect shader stage from extension
    auto stageResult = detect_stage(glslPath);
    if (!stageResult)
        return make_error(stageResult.error());

    // Read source
    std::ifstream file(glslPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open shader source: {}", glslPath.string()),
                          ErrorCode::FileReadFailed);

    auto size = file.tellg();
    file.seekg(0);
    std::string source(static_cast<size_t>(size), '\0');
    file.read(source.data(), size);

    return compile(source, stageResult.value(), glslPath.filename().string());
}

Result<std::vector<uint32_t>> ShaderCompiler::compile_or_cache(const fs::path& glslPath, const fs::path& spvPath)
{
    // Check if cached .spv exists and is newer than the source
    if (fs::exists(spvPath) && fs::exists(glslPath))
    {
        auto srcTime = fs::last_write_time(glslPath);
        auto spvTime = fs::last_write_time(spvPath);

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
    fs::create_directories(spvPath.parent_path());
    auto writeResult = write_spv(spvPath, spirvResult.value());
    if (!writeResult)
        fmt::print("Warning: failed to cache shader: {}\n", writeResult.error().message);

    return spirvResult;
}

fs::path ShaderCompiler::get_spv_path(const fs::path& glslPath)
{
    fs::path result = glslPath;
    result += ".spv"; // e.g. shader.vert -> shader.vert.spv
    return result;
}

// ── Private helpers ──────────────────────────────────────────────────

Result<ShaderStage> ShaderCompiler::detect_stage(const fs::path& glslPath)
{
    auto ext = glslPath.extension().string();
    if (ext == ".vert")
        return ShaderStage::Vertex;
    if (ext == ".frag")
        return ShaderStage::Fragment;

    return make_error(fmt::format("Unknown shader extension '{}' — expected .vert or .frag", ext),
                      ErrorCode::ShaderCompilationFailed);
}

Result<std::vector<uint32_t>> ShaderCompiler::read_spv(const fs::path& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open cached shader: {}", spvPath.string()), ErrorCode::FileReadFailed);

    auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize == 0 || fileSize % sizeof(uint32_t) != 0)
        return make_error(fmt::format("Invalid SPIR-V file (size {}): {}", fileSize, spvPath.string()),
                          ErrorCode::ShaderCompilationFailed);

    file.seekg(0);
    std::vector<uint32_t> spirv(fileSize / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(fileSize));

    return spirv;
}

Result<> ShaderCompiler::write_spv(const fs::path& spvPath, const std::vector<uint32_t>& spirv)
{
    std::ofstream file(spvPath, std::ios::binary);
    if (!file.is_open())
        return make_error(fmt::format("Failed to open shader cache for writing: {}", spvPath.string()),
                          ErrorCode::AssetCacheWriteFailed);

    file.write(reinterpret_cast<const char*>(spirv.data()),
               static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
    if (!file.good())
        return make_error(fmt::format("Failed to write shader cache: {}", spvPath.string()),
                          ErrorCode::AssetCacheWriteFailed);

    return {};
}
