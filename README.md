# NatureOfCraft

A Vulkan + GLFW + fmt C++23 project. You can build it either with Conan 2 + Premake5 (recommended) or with the legacy `nob` builder.

## Prerequisites

- Linux (x86_64)
- C/C++ toolchain
  - GCC or Clang (C++23 capable)
- Conan 2.x
- Premake 5.x
- glslc (for shader compilation)
  - Provided by the Vulkan SDK or shaderc packages in your distro
- Vulkan drivers/runtime installed on your system
  - Ensure your system Vulkan ICD/driver is installed (e.g., Mesa Vulkan drivers)

Tip:

- Ensure `glslc`, `premake5`, and your compiler are in PATH.
- Conan will provide fmt, glfw, vulkan-loader, and headers automatically.

## Project layout

- Source: `src/`
- Build artifacts:
  - Premake objects: `build/Debug`, `build/Release`
  - Binaries: `bin/Debug/NatureOfCraft`, `bin/Release/NatureOfCraft`
- Conan generator output: `build_conan/`
- Shaders (compiled automatically by Premake prebuild commands):
  - `src/Resources/shader.vert` -> `src/Resources/vert.spv`
  - `src/Resources/shader.frag` -> `src/Resources/frag.spv`

## Build with Conan 2 + Premake5 (recommended)

The build uses Conan’s Premake generators and a Premake5 project that sets C++23 and wires everything up.

1. Install dependencies with Conan

Do this once per build type you intend to build:

Debug:

```bash
conan install . --output-folder=build_conan --build=missing -s build_type=Debug
```

Release:

```bash
conan install . --output-folder=build_conan --build=missing -s build_type=Release
```

This generates `build_conan/conantoolchain.premake5.lua` and `build_conan/conandeps.premake5.lua` used by Premake.

2. Generate the Makefiles with Premake

```bash
premake5 gmake
```

3. Build

Debug:

```bash
make clean config=debug
make config=debug -j
```

Release:

```bash
make clean config=release
make config=release -j
```

The shaders are compiled automatically by prebuild commands using `glslc`.

4. Run

It’s best to activate the Conan runtime environment so dynamic libraries are located correctly:

```bash
source build_conan/conanrun.sh
./bin/Debug/NatureOfCraft
# or
./bin/Release/NatureOfCraft
```

Notes:

- If you change dependencies or switch compilers, re-run the `conan install` step for the corresponding build type and regenerate with `premake5 gmake`.
- If you encounter runtime loader issues, ensure you sourced `build_conan/conanrun.sh`.

## Editor/IDE integration (clangd)

To generate `compile_commands.json` for clangd using the Premake + Make build, you can use Bear:

```bash
rm -f compile_commands.json
bear -- make clean config=debug
bear -- make config=debug -j
```

This will create `compile_commands.json` at the project root, which clangd can use.

## Legacy: Build with the `nob` builder

This project also includes a small C build script (`nob.c`) that can directly compile and link the project with clang++. It does not use Conan and expects dependencies available on the system (e.g., `-lfmt -lglfw -lvulkan`). Keep using it only if you know your system provides everything needed.

1. Build the builder:

```bash
cc nob.c -o nob
```

2. Build and run:

Debug (default):

```bash
./nob
# or explicitly
./nob debug
```

Release:

```bash
./nob release
```

If you modify `nob.c`, just run `./nob` again; it can self-rebuild using `nob.h` logic.

## Troubleshooting

- Undefined references to Vulkan/GLFW/fmt:
  - Make sure you ran `conan install` for the correct build type (Debug/Release).
  - Clean stale objects: `make clean config=debug` (or release) and rebuild.
  - Regenerate Makefiles if needed: `premake5 gmake2`.

- glslc not found:
  - Install a package that provides `glslc` (commonly from the Vulkan SDK or shaderc packages in your distro).

- Vulkan runtime issues:
  - Ensure your system’s Vulkan driver/ICD is installed and discoverable (e.g., Mesa Vulkan drivers).

- Switching compilers (e.g., to Clang):
  - Export environment variables before building:
    ```bash
    export CC=clang
    export CXX=clang++
    premake5 gmake2
    make clean config=debug && make config=debug -j
    ```
  - Re-run the `conan install` steps if necessary, so ABI and libstdc++ variants match your compiler/runtime.
