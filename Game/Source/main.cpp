#define NOMINMAX
#include <Assets/Public/AssetManager.hpp>
#include <Camera/Public/Camera.hpp>
#include <Core/Public/RuntimePaths.hpp>
#include <Level/Public/Level.hpp>
#include <Level/Public/Project.hpp>
#include <Physics/Public/PhysicsWorld.hpp>
#include <Rendering/BackEnds/Public/Vulkan.hpp>
#include <Runtime/Public/ProjectPipeline.hpp>
#include <Scripting/Public/ScriptEngine.hpp>
#include <Window/Public/Window.hpp>

#include <fmt/core.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>

namespace
{
constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;

struct LaunchOptions
{
    std::filesystem::path projectFile;
    std::filesystem::path levelFile;
    std::filesystem::path contentRoot;
    std::filesystem::path userDataRoot;
    bool validateStartup{false};
};

void print_usage()
{
    fmt::print("Usage: Game [--project <path>] [--level <path>] [--content-root <path>] [--user-data-root <path>] "
               "[--validate-startup]\n");
}

Result<LaunchOptions> parse_launch_options(int argc, char** argv)
{
    LaunchOptions options;
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
        else if (arg == "--level")
        {
            if (auto result = require_value(options.levelFile); !result)
                return make_error(result.error());
        }
        else if (arg == "--content-root")
        {
            if (auto result = require_value(options.contentRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--user-data-root")
        {
            if (auto result = require_value(options.userDataRoot); !result)
                return make_error(result.error());
        }
        else if (arg == "--validate-startup")
        {
            options.validateStartup = true;
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

    return options;
}

std::filesystem::path resolve_cli_path(const std::filesystem::path& inputPath, const std::filesystem::path& basePath)
{
    if (inputPath.empty())
        return {};
    if (inputPath.is_absolute())
        return inputPath;

    const std::filesystem::path baseResolved = basePath / inputPath;
    if (std::filesystem::exists(baseResolved))
        return baseResolved;

    std::error_code ec;
    return std::filesystem::absolute(inputPath, ec);
}

Result<std::filesystem::path> discover_project_file(const LaunchOptions& options, const RuntimePaths& runtimePaths)
{
    if (!options.projectFile.empty())
        return resolve_cli_path(options.projectFile, runtimePaths.game_content_dir());

    std::error_code ec;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(runtimePaths.game_content_dir(),
                                                       std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".noc_project")
            return entry.path();
    }

    return make_error("No cooked .noc_project file was found under the game content directory",
                      ErrorCode::AssetFileNotFound);
}
} // namespace

int main(int argc, char** argv)
{
    auto optionsResult = parse_launch_options(argc, argv);
    if (!optionsResult)
    {
        if (optionsResult.error().message != "help")
            fmt::print("{}\n", optionsResult.error().message);
        return optionsResult.error().message == "help" ? 0 : -1;
    }
    LaunchOptions options = std::move(optionsResult.value());

    if (auto runtimePathsResult =
            RuntimePaths::initialize_current_process("NatureOfCraft", argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path{},
                                                     options.contentRoot, options.userDataRoot);
        !runtimePathsResult)
    {
        fmt::print("Failed to initialize runtime paths: {}\n", runtimePathsResult.error().message);
        return -1;
    }

    const RuntimePaths& runtimePaths = RuntimePaths::current();
    auto projectFileResult = discover_project_file(options, runtimePaths);
    if (!projectFileResult)
    {
        fmt::print("{}\n", projectFileResult.error().message);
        return -1;
    }

    Window window{kWindowWidth, kWindowHeight, "NatureOfCraft"};
    if (auto code = get_error_code(window.init()); code != 0)
        return code;

    Vulkan renderer{window.get_glfw_window()};
    renderer.set_shader_load_mode(ShaderLoadMode::PrecompiledOnly);
    window.set_framebuffer_size_callback([&renderer](std::int32_t, std::int32_t) { renderer.on_framebuffer_resized(); });
    if (auto code = get_error_code(renderer.initialize()); code != 0)
        return code;

    AssetManager assetManager;
    ScriptEngine scriptEngine;
    PhysicsWorld physicsWorld;
    if (auto initResult = scriptEngine.initialize(); !initResult)
    {
        fmt::print("Failed to initialize ScriptEngine: {}\n", initResult.error().message);
        return -1;
    }
    if (auto initResult = physicsWorld.initialize(); !initResult)
    {
        fmt::print("Failed to initialize PhysicsWorld: {}\n", initResult.error().message);
        return -1;
    }

    auto startupBegin = std::chrono::steady_clock::now();
    auto projectResult = Project::load(projectFileResult.value().string());
    if (!projectResult)
    {
        fmt::print("Failed to load project '{}': {}\n", projectFileResult.value().string(), projectResult.error().message);
        return -1;
    }
    Project project = std::move(projectResult.value());

    std::filesystem::path levelPath;
    if (!options.levelFile.empty())
        levelPath = resolve_cli_path(options.levelFile, project.root_path());
    else if (!project.levels().empty())
        levelPath = project.get_absolute_path(project.levels().front().filePath);
    else
    {
        fmt::print("Project '{}' does not contain any levels.\n", project.name());
        return -1;
    }

    auto levelLoadBegin = std::chrono::steady_clock::now();
    auto levelResult = Level::load(levelPath.string());
    if (!levelResult)
    {
        fmt::print("Failed to load level '{}': {}\n", levelPath.string(), levelResult.error().message);
        return -1;
    }
    Level level = std::move(levelResult.value());

    RuntimeLoadOptions loadOptions;
    loadOptions.seedProjectDefaults = false;
    loadOptions.allowEmbeddedMaterialTextureExtraction = false;
    loadOptions.requireCookedModels = true;
    loadOptions.requireGlowShaders = false;
    auto prepareResult = prepare_loaded_level(assetManager, renderer, scriptEngine, physicsWorld, level, &project, loadOptions);
    if (!prepareResult)
    {
        fmt::print("Failed to prepare level runtime: {}\n", prepareResult.error().message);
        return -1;
    }
    for (const auto& warning : prepareResult->warnings)
        fmt::print("Warning: {}\n", warning);

    OrbitCameraController cameraController;
    entt::entity activeCamera = level.world().get_active_camera();
    if (activeCamera == entt::null)
    {
        fmt::print("The loaded level does not have an active camera.\n");
        return -1;
    }
    cameraController.attach(level.world().registry(), activeCamera);
    physicsWorld.set_enabled(true);

    auto update_frame = [&](float deltaTime) -> Result<> {
        if (entt::entity currentActiveCamera = level.world().get_active_camera();
            currentActiveCamera != entt::null && currentActiveCamera != activeCamera)
        {
            activeCamera = currentActiveCamera;
            cameraController.attach(level.world().registry(), activeCamera);
        }

        const std::uint32_t renderWidth = renderer.get_render_width();
        const std::uint32_t renderHeight = renderer.get_render_height();
        const float aspectRatio = renderHeight > 0 ? static_cast<float>(renderWidth) / static_cast<float>(renderHeight) : 1.0f;
        renderer.set_view_projection(cameraController.get_view_matrix(), cameraController.get_projection_matrix(aspectRatio));

        World& world = level.world();
        scriptEngine.update(world, deltaTime);
        physicsWorld.step(world, deltaTime);
        world.update_world_matrices();
        if (world.has_renderables_updates_pending())
            renderer.set_renderables(world.collect_renderables());

        return renderer.draw_frame();
    };

    const double levelLoadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - levelLoadBegin).count();
    const double coldStartMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startupBegin).count();

    if (options.validateStartup)
    {
        auto firstFrameBegin = std::chrono::steady_clock::now();
        auto drawResult = update_frame(1.0f / 60.0f);
        if (!drawResult)
        {
            fmt::print("First frame validation failed: {}\n", drawResult.error().message);
            return -1;
        }

        renderer.wait_idle();
        const double firstFrameMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - firstFrameBegin).count();
        fmt::print("validation.cold_start_ms={:.2f}\n", coldStartMs);
        fmt::print("validation.level_load_ms={:.2f}\n", levelLoadMs);
        fmt::print("validation.first_frame_ms={:.2f}\n", firstFrameMs);
        fmt::print("validation.mesh_uploads={}\n", prepareResult->totalMeshes);
        fmt::print("validation.material_uploads={}\n", prepareResult->totalMaterials);
        fmt::print("validation.used_raw_source_assets={}\n", prepareResult->usedRawSourceAssets ? "true" : "false");
        return 0;
    }

    auto lastFrameTime = std::chrono::steady_clock::now();
    auto loopResult = window.loop([&]() -> Result<> {
        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        return update_frame(deltaTime);
    });

    renderer.wait_idle();
    if (!loopResult)
    {
        fmt::print("Game loop failed: {}\n", loopResult.error().message);
        return -1;
    }

    return 0;
}
