# NatureOfCraft

A Vulkan-based C++26 project using CMake, vcpkg, and GLFW.

## Prerequisites

- **Windows** (x64) or **Linux** (x86_64)
- **C/C++ toolchain**
  - MSVC (Visual Studio 2022/2026) on Windows
  - GCC or Clang (C++26 capable) on Linux
- **CMake** 3.10 or higher
- **vcpkg** (integrated with Visual Studio or standalone)
- **Vulkan SDK** (optional, vcpkg provides vulkan-loader/headers)
  - Required for `glslc` shader compiler if not using vcpkg's shaderc

## Dependencies (managed by vcpkg)

- `fmt` - Formatting library
- `glfw3` - Window/input handling
- `imgui` - Immediate mode GUI (with GLFW and Vulkan bindings)
- `shaderc` - Shader compilation (provides `glslc`)
- `vulkan`, `vulkan-headers`, `vulkan-loader` - Vulkan API

## Building with Visual Studio (Windows)

### Using Visual Studio's CMake Integration

1. **Open the project folder** in Visual Studio 2022 or later
   - Visual Studio will automatically detect `CMakeLists.txt` and configure the project

2. **vcpkg integration**
   - If using Visual Studio's integrated vcpkg, dependencies are installed automatically
   - Otherwise, ensure `CMAKE_TOOLCHAIN_FILE` points to your vcpkg installation:
     ```
     -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
     ```

3. **Select configuration**
   - Choose `x64-Debug` or `x64-Release` from the configuration dropdown

4. **Build**
   - Use `Build > Build All` or press `Ctrl+Shift+B`

5. **Run**
   - Set `Editor` as the startup project and run with `F5`

### Using CMake from Command Line (Windows)

```powershell
# Configure (Debug)
cmake -B out/build/x64-debug -S . -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_TOOLCHAIN_FILE="<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build out/build/x64-debug

# Run
./out/build/x64-debug/Editor/Editor.exe
```

## Building on Linux

```bash
# Configure (Debug)
cmake -B build -S . -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build

# Run
./build/Editor/Editor
```

## Shader Compilation

Shaders are automatically compiled during the build process:
- `Editor/Resources/shader.vert` → `vert.spv`
- `Editor/Resources/shader.frag` → `frag.spv`

The build system uses `glslc` from either:
- Vulkan SDK (`$VULKAN_SDK/Bin`)
- vcpkg's shaderc package

## Editor/IDE Integration

### Visual Studio
The project is fully integrated with Visual Studio's CMake support. IntelliSense works out of the box.

### clangd / VS Code
A `compile_commands.json` is generated in the build directory. Either:
- Symlink it to the project root: `ln -s out/build/x64-debug/compile_commands.json .`
- Or configure your editor to look in the build directory

## Troubleshooting

### glslc not found
- Install the Vulkan SDK, or
- Ensure vcpkg's `shaderc` package is installed (it's in `vcpkg.json`)

### Undefined references to Vulkan/GLFW/fmt
- Ensure vcpkg dependencies are installed: check that `vcpkg_installed/` directory exists
- Clean and rebuild: delete the `out/` folder and reconfigure

### Runtime errors finding DLLs (Windows)
- The build copies `Source.dll` to the Editor output directory automatically
- Ensure vcpkg DLLs are in PATH or copied to the output directory

### Vulkan validation layer errors
- Install the Vulkan SDK for validation layers
- On Windows, layers are typically at `%VULKAN_SDK%\Bin`
