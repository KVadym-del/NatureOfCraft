#define NOMINMAX
#include <Core/Public/RuntimePaths.hpp>
#include <Runtime/Public/ProjectPipeline.hpp>

#include <fmt/core.h>

#include <utility>
#include <filesystem>
#include <string_view>

namespace
{
struct CookOptions
{
    std::filesystem::path projectFile;
    std::filesystem::path outputRoot;
    std::filesystem::path bundleOutputRoot;
    std::filesystem::path runtimeBinaryRoot;
    std::filesystem::path contentRoot;
    std::filesystem::path userDataRoot;
    bool compileShaders{true};
    bool strict{true};
};

void print_usage()
{
    fmt::print("Usage: NatureOfCraftCooker --project <path> (--output <dir> | --bundle-output <dir>) "
               "[--runtime-dir <path>] [--content-root <path>] [--user-data-root <path>] "
               "[--no-compile-shaders] [--no-strict]\n");
}

Result<CookOptions> parse_options(int argc, char** argv)
{
    CookOptions options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        auto require_value = [&](std::filesystem::path& outPath) -> Result<> {
            if (i + 1 >= argc)
                return make_error(fmt::format("Missing value for '{}'", arg), ErrorCode::AssetFileNotFound);
            outPath = argv[++i];
            return {};
        };

        if (arg == "--project")
        {
            if (auto result = require_value(options.projectFile); !result)
                return make_error(result.error());
        }
        else if (arg == "--output")
        {
            if (auto result = require_value(options.outputRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--content-root")
        {
            if (auto result = require_value(options.contentRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--bundle-output")
        {
            if (auto result = require_value(options.bundleOutputRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--runtime-dir")
        {
            if (auto result = require_value(options.runtimeBinaryRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--user-data-root")
        {
            if (auto result = require_value(options.userDataRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--no-compile-shaders")
        {
            options.compileShaders = false;
        }
        else if (arg == "--no-strict")
        {
            options.strict = false;
        }
        else if (arg == "--help" || arg == "-h")
        {
            print_usage();
            return make_error("help", ErrorCode::None);
        }
        else
        {
            return make_error(fmt::format("Unknown argument '{}'", arg), ErrorCode::AssetFileNotFound);
        }
    }

    if (options.projectFile.empty())
        return make_error("--project is required", ErrorCode::AssetFileNotFound);
    if (options.outputRoot.empty() && options.bundleOutputRoot.empty())
        return make_error("Either --output or --bundle-output is required", ErrorCode::AssetFileNotFound);

    return options;
}
} // namespace

int main(int argc, char** argv)
{
    auto optionsResult = parse_options(argc, argv);
    if (!optionsResult)
    {
        if (optionsResult.error().message != "help")
            fmt::print("{}\n", optionsResult.error().message);
        return optionsResult.error().message == "help" ? 0 : -1;
    }

    CookOptions options = std::move(optionsResult.value());
    if (auto runtimePathsResult =
            RuntimePaths::initialize_current_process("NatureOfCraft", argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path{},
                                                     options.contentRoot, options.userDataRoot);
        !runtimePathsResult)
    {
        fmt::print("Failed to initialize runtime paths: {}\n", runtimePathsResult.error().message);
        return -1;
    }

    if (!options.bundleOutputRoot.empty())
    {
        BundleProjectOptions bundleOptions;
        bundleOptions.outputRoot = options.bundleOutputRoot;
        bundleOptions.runtimeBinaryRoot = options.runtimeBinaryRoot;
        bundleOptions.overwriteOutput = true;
        bundleOptions.compileShaders = options.compileShaders;
        bundleOptions.strict = options.strict;
        auto bundleResult = bundle_project(options.projectFile, bundleOptions);
        if (!bundleResult)
        {
            fmt::print("Bundle failed: {}\n", bundleResult.error().message);
            return -1;
        }

        fmt::print("Bundle root: {}\n", bundleResult->bundleRoot.string());
        fmt::print("Bundle content root: {}\n", bundleResult->contentRoot.string());
        fmt::print("Cooked project: {}\n", bundleResult->cookResult.cookedProjectFile.string());
        fmt::print("Cooked levels: {}\n", bundleResult->cookResult.cookedLevelCount);
        fmt::print("Cooked raw models: {}\n", bundleResult->cookResult.cookedModelCount);
        fmt::print("Copied textures: {}\n", bundleResult->cookResult.copiedTextureCount);
        fmt::print("Copied materials: {}\n", bundleResult->cookResult.copiedMaterialCount);
        fmt::print("Copied scripts: {}\n", bundleResult->cookResult.copiedScriptCount);
        fmt::print("Copied engine files: {}\n", bundleResult->cookResult.copiedEngineFileCount);
        fmt::print("Copied runtime files: {}\n", bundleResult->copiedRuntimeFileCount);
        for (const auto& warning : bundleResult->warnings)
            fmt::print("Warning: {}\n", warning);
        return 0;
    }

    CookProjectOptions cookOptions;
    cookOptions.outputRoot = options.outputRoot;
    cookOptions.overwriteOutput = true;
    cookOptions.compileShaders = options.compileShaders;
    cookOptions.strict = options.strict;
    auto cookResult = cook_project(options.projectFile, cookOptions);
    if (!cookResult)
    {
        fmt::print("Cook failed: {}\n", cookResult.error().message);
        return -1;
    }

    fmt::print("Cooked project: {}\n", cookResult->cookedProjectFile.string());
    fmt::print("Cooked levels: {}\n", cookResult->cookedLevelCount);
    fmt::print("Cooked raw models: {}\n", cookResult->cookedModelCount);
    fmt::print("Copied textures: {}\n", cookResult->copiedTextureCount);
    fmt::print("Copied materials: {}\n", cookResult->copiedMaterialCount);
    fmt::print("Copied scripts: {}\n", cookResult->copiedScriptCount);
    fmt::print("Copied engine files: {}\n", cookResult->copiedEngineFileCount);
    for (const auto& warning : cookResult->warnings)
        fmt::print("Warning: {}\n", warning);

    return 0;
}
