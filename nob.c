#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_EXPERIMENTAL_DELETE_OLD
#define NOB_WARN_DEPRECATED
#include "nob.h"

#define BUILD_FOLDER "build/"
#define BIN_FOLDER "bin/"
#define DEBUG_FOLDER "debug/"
#define RELEASE_FOLDER "release/"

#define DEBUG_FLAGS "-g", "-O0", "-DDEBUG", "-Wall", "-Wextra"
#define RELEASE_FLAGS "-O3", "-DNDEBUG", "-Wall", "-Wextra"

#define CPP_VERSION "c++23"

#define LIBS "-lfmt", "-lglfw", "-lvulkan"

#define PROGRAM_NAME "NatureOfCraft"

const char** src_files = (const char*[]){
    "src/main.cpp",
    "src/Window/Private/Window.cpp",
    "src/Rendering/BackEnds/Private/Vulkan.cpp",
    NULL,
};

typedef struct
{
    const char* src;
    const char* out;
} ShaderPair;

const ShaderPair shader_files[] = {
    {"src/Resources/shader.vert", "src/Resources/vert.spv"},
    {"src/Resources/shader.frag", "src/Resources/frag.spv"},
    {NULL, NULL},
};

bool build_and_run(Cmd* cmd, bool debug)
{
    size_t mark = temp_save();

    const char* build_folder = debug ? BUILD_FOLDER DEBUG_FOLDER : BUILD_FOLDER RELEASE_FOLDER;
    const char* bin_folder = debug ? BIN_FOLDER DEBUG_FOLDER : BIN_FOLDER RELEASE_FOLDER;

    const char* bin_path = temp_sprintf("%s%s", bin_folder, PROGRAM_NAME);

    if (debug)
    {
        nob_log(NOB_INFO, "--- Building %s in Debug mode ---", bin_path);
    }
    else
    {
        nob_log(INFO, "--- Building %s in Release mode ---", bin_path);
    }

    for (const char** src = src_files; *src != NULL; src++)
    {
        const char* src_path = temp_sprintf("%s", *src);
        const char* base_name = strrchr(*src, '/');
        base_name = base_name ? base_name + 1 : *src;
        const char* ext = strrchr(base_name, '.');
        size_t name_len = ext ? (size_t)(ext - base_name) : strlen(base_name);
        const char* obj_name = temp_sprintf("%.*s.o", (int)name_len, base_name);
        const char* obj_path = temp_sprintf("%s%s", build_folder, obj_name);

        cmd->count = 0;
        nob_cmd_append(cmd, "clang++");
        if (debug)
        {
            nob_cmd_append(cmd, DEBUG_FLAGS);
        }
        else
        {
            nob_cmd_append(cmd, RELEASE_FLAGS);
        }
        nob_cmd_append(cmd, nob_temp_sprintf("-std=%s", CPP_VERSION));
        nob_cmd_append(cmd, "-c");
        nob_cmd_append(cmd, src_path);
        nob_cmd_append(cmd, "-o", obj_path);

        if (!cmd_run(cmd))
            return false;
    }

    cmd->count = 0;
    nob_cmd_append(cmd, "clang++");
    if (debug)
    {
        nob_cmd_append(cmd, DEBUG_FLAGS);
    }
    else
    {
        nob_cmd_append(cmd, RELEASE_FLAGS);
    }
    nob_cmd_append(cmd, nob_temp_sprintf("-std=%s", CPP_VERSION));

    for (const char** src = src_files; *src != NULL; src++)
    {
        const char* base_name = strrchr(*src, '/');
        base_name = base_name ? base_name + 1 : *src;
        const char* ext = strrchr(base_name, '.');
        size_t name_len = ext ? (size_t)(ext - base_name) : strlen(base_name);
        const char* obj_name = temp_sprintf("%.*s.o", (int)name_len, base_name);
        const char* obj_path = temp_sprintf("%s%s", build_folder, obj_name);
        nob_cmd_append(cmd, obj_path);
    }

    nob_cmd_append(cmd, LIBS);

    nob_cmd_append(cmd, "-o", bin_path);
    if (!cmd_run(cmd))
        return false;

    // Compile shaders
    nob_log(INFO, "--- Compiling shaders ---");
    for (size_t i = 0; shader_files[i].src != NULL; i++)
    {
        const char* shader_src = shader_files[i].src;
        const char* shader_out = shader_files[i].out;

        cmd->count = 0;
        nob_cmd_append(cmd, "glslc");
        nob_cmd_append(cmd, shader_src);
        nob_cmd_append(cmd, "-o", shader_out);

        if (!cmd_run(cmd))
            return false;
    }

    nob_log(INFO, "--- %s finished ---", bin_path);

    temp_rewind(mark);
    return true;
}

int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "nob.h");

    Cmd cmd = {0};

    shift(argv, argc);
    const char* buildModeDebugCommand = "debug";
    const char* buildModeReleaseCommand = "release";
    const char* command_name = NULL;
    if (argc > 0)
        command_name = shift(argv, argc);

    if (!mkdir_if_not_exists(BUILD_FOLDER))
        return 1;
    if (!mkdir_if_not_exists(BIN_FOLDER))
        return 1;
    if (!mkdir_if_not_exists(BUILD_FOLDER DEBUG_FOLDER))
        return 1;
    if (!mkdir_if_not_exists(BUILD_FOLDER RELEASE_FOLDER))
        return 1;
    if (!mkdir_if_not_exists(BIN_FOLDER DEBUG_FOLDER))
        return 1;
    if (!mkdir_if_not_exists(BIN_FOLDER RELEASE_FOLDER))
        return 1;

    if (command_name != NULL && strcmp(command_name, buildModeDebugCommand) == 0)
    {
        if (!build_and_run(&cmd, true))
            return 1;
        return 0;
    }

    if (command_name != NULL && strcmp(command_name, buildModeReleaseCommand) == 0)
    {
        if (!build_and_run(&cmd, false))
            return 1;
        return 0;
    }

    if (!build_and_run(&cmd, true))
        return 1;
    return 0;
}
