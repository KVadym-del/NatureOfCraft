#pragma once
#include "../../Core/Public/Core.hpp"
#include "../../Core/Public/Expected.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

NOC_SUPPRESS_DLL_WARNINGS

/// Shader stage used to select the correct shaderc shader kind.
enum class ShaderStage
{
    Vertex,
    Fragment,
};

/// Compiles GLSL shader source to SPIR-V bytecode at runtime using shaderc.
/// Supports file-based caching: writes compiled .spv next to the source file
/// and reuses it on subsequent loads unless the source is newer.
class NOC_EXPORT ShaderCompiler
{
  public:
    /// Compiles a GLSL source string to SPIR-V bytecode.
    /// @param glslSource  The GLSL source code.
    /// @param stage       Vertex or Fragment.
    /// @param filename    A label used in error messages (does not need to exist on disk).
    static Result<std::vector<std::uint32_t>> compile(std::string_view glslSource, ShaderStage stage,
                                                      std::string_view filename = "shader");

    /// Reads a GLSL file from disk and compiles it to SPIR-V.
    /// @param glslPath    Path to the .vert / .frag file.
    static Result<std::vector<std::uint32_t>> compile_file(const std::filesystem::path& glslPath);

    /// Compiles a GLSL file to SPIR-V, writing the result to spvPath.
    /// If spvPath already exists and is newer than glslPath, reads the cached .spv instead.
    /// @param glslPath    Path to the GLSL source file.
    /// @param spvPath     Path where compiled .spv should be written / read.
    static Result<std::vector<std::uint32_t>> compile_or_cache(const std::filesystem::path& glslPath,
                                                               const std::filesystem::path& spvPath);

    /// Convenience: derives the .spv path by replacing the extension.
    /// e.g. "Assets/Shaders/shader.vert" -> "Assets/Shaders/shader.vert.spv"
    static std::filesystem::path get_spv_path(const std::filesystem::path& glslPath);

  private:
    /// Detects shader stage from file extension (.vert / .frag).
    static Result<ShaderStage> detect_stage(const std::filesystem::path& glslPath);

    /// Reads a .spv binary file into a std::uint32_t vector.
    static Result<std::vector<std::uint32_t>> read_spv(const std::filesystem::path& spvPath);

    /// Writes SPIR-V data to a binary file.
    static Result<> write_spv(const std::filesystem::path& spvPath, const std::vector<std::uint32_t>& spirv);
};

NOC_RESTORE_DLL_WARNINGS
