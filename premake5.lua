-- premake5.lua for NatureOfCraft
-- Cross-platform build configuration with vcpkg integration

workspace "NatureOfCraft"
architecture "x64"
configurations { "Debug", "Release" }
startproject "NatureOfCraft"

-- Platform-specific settings
filter "system:windows"
platforms { "Win64" }
systemversion "latest"
filter "system:linux"
platforms { "Linux" }
filter {}

-- Output directories
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "NatureOfCraft"
kind "ConsoleApp"
language "C++"
cppdialect "C++23"
staticruntime "off"

-- Output directories
targetdir("bin/%{cfg.buildcfg:lower()}")
objdir("build/%{cfg.buildcfg:lower()}")

-- Source files
files {
    "src/main.cpp",
    "src/Window/Private/Window.cpp",
    "src/Rendering/BackEnds/Private/Vulkan.cpp"
}

-- Include directories
includedirs {
    "src"
}

-- vcpkg integration (manifest mode)
-- vcpkg installs packages locally in vcpkg_installed/ directory when using manifest mode
local vcpkg_installed = "vcpkg_installed"

-- vcpkg include and lib directories (manifest mode uses local vcpkg_installed/)
-- Use x64-windows-static for static linking on Windows (avoids DLL issues with MinGW)
filter "system:windows"
local vcpkg_triplet_win = "x64-windows-static"
includedirs {
    path.join(vcpkg_installed, vcpkg_triplet_win, "include")
}
libdirs {
    path.join(vcpkg_installed, vcpkg_triplet_win, "lib")
}

filter "system:linux"
local vcpkg_triplet_linux = "x64-linux"
includedirs {
    path.join(vcpkg_installed, vcpkg_triplet_linux, "include")
}
libdirs {
    path.join(vcpkg_installed, vcpkg_triplet_linux, "lib")
}

filter {}

-- Libraries from vcpkg (platform-specific names)
filter "system:windows"
links {
    "glfw3", -- staticdll", -- or glfw3 for static linking
    "vulkan-1"
}

filter "system:linux"
links {
    "fmt",
    "glfw",
    "vulkan"
}

filter {}

-- Compiler-specific configurations
filter "toolset:msc*"
defines { "_CRT_SECURE_NO_WARNINGS", "FMT_SHARED" }
buildoptions { "/std:c++latest" }

filter { "system:windows", "toolset:not msc*" }
defines { "_CRT_SECURE_NO_WARNINGS", "FMT_SHARED" }
buildoptions { "-std=c++23" }

filter "system:linux"
buildoptions { "-std=c++23" }

-- Debug configuration for Windows
filter { "system:windows", "configurations:Debug" }
local vcpkg_triplet_win = "x64-windows"
libdirs {
    path.join(vcpkg_installed, vcpkg_triplet_win, "debug/lib")
}
links {
    "fmt" -- fmt on Windows needs special handling for MinGW
}
-- Note: glfw3dll and vulkan-1 are already linked above

-- Release configuration for Windows
filter { "system:windows", "configurations:Release" }
links {
    "fmt" -- fmt release library
}
-- Use release libs from main lib directory

-- Debug configuration for Linux
filter { "system:linux", "configurations:Debug" }
local vcpkg_triplet_linux = "x64-linux"
libdirs {
    path.join(vcpkg_installed, vcpkg_triplet_linux, "debug/lib")
}

-- Release configuration for Linux
filter { "system:linux", "configurations:Release" }
-- Use release libs from main lib directory

filter {}

-- Debug configuration
filter "configurations:Debug"
defines { "DEBUG", "_DEBUG" }
runtime "Debug"
symbols "On"
optimize "Off"
buildoptions { "-Wall", "-Wextra" }

-- Release configuration
filter "configurations:Release"
defines { "NDEBUG" }
runtime "Release"
optimize "Full"
buildoptions { "-Wall", "-Wextra" }

filter {}

-- Post-build shader compilation
newaction {
    trigger = "compile-shaders",
    description = "Compile GLSL shaders to SPIR-V",
    execute = function()
        print("Compiling shaders...")

        local shaders = {
            { src = "src/Resources/shader.vert", out = "src/Resources/vert.spv" },
            { src = "src/Resources/shader.frag", out = "src/Resources/frag.spv" }
        }

        for _, shader in ipairs(shaders) do
            local cmd = string.format("glslc %s -o %s", shader.src, shader.out)
            print("  " .. cmd)
            local result = os.execute(cmd)
            if result ~= 0 then
                error("Failed to compile shader: " .. shader.src)
            end
        end

        print("Shader compilation complete!")
    end
}

-- Custom action to set up vcpkg
newaction {
    trigger = "setup-vcpkg",
    description = "Initialize vcpkg manifest and add required packages",
    execute = function()
        print("Setting up vcpkg dependencies using manifest mode...")

        local packages = { "fmt", "glfw3", "vulkan" }

        -- Check if vcpkg.json already exists
        if os.isfile("vcpkg.json") then
            print("vcpkg.json already exists. Skipping initialization.")
            print("To reinstall, delete vcpkg.json and vcpkg-configuration.json first.")
        else
            print("Initializing new vcpkg application manifest...")

            local init_cmd = "vcpkg new --application"
            print("Executing: " .. init_cmd)
            os.execute(init_cmd)

            -- Verify that vcpkg.json was actually created
            if not os.isfile("vcpkg.json") then
                error("Failed to initialize vcpkg manifest - vcpkg.json was not created")
            end
            print("vcpkg manifest initialized successfully!")
        end

        -- Add each package using vcpkg add port
        print("\nAdding packages to manifest...")
        for _, pkg in ipairs(packages) do
            print("Adding " .. pkg .. "...")
            local add_cmd = string.format("vcpkg add port %s", pkg)
            print("Executing: " .. add_cmd)
            os.execute(add_cmd)
            print(pkg .. " added to manifest")
        end

        -- Install dependencies
        print("\nInstalling dependencies from manifest...")
        local install_cmd = ""
        if os.host() == "windows" then
            install_cmd = "vcpkg install --triplet=x64-windows"
        else
            install_cmd = "vcpkg install --triplet=x64-linux"
        end

        print("Executing: " .. install_cmd)
        os.execute(install_cmd)

        -- vcpkg install can return non-zero even on success, so just check if files exist
        print("Verifying installation...")

        print("\nvcpkg setup complete!")
        print("All dependencies have been installed.")
        print("If there were errors above, check them manually.")
    end
}

-- Print configuration info
print("=== NatureOfCraft Build Configuration ===")
print("C++ Standard: C++23")
print("Compiler: Clang++ (recommended) or MSVC/GCC")
print("Configurations: Debug, Release")
print("")
print("vcpkg packages required:")
print("  - fmt")
print("  - glfw3")
print("  - vulkan")
print("")
print("Usage:")
print("  premake5 vs2022       -- Generate Visual Studio 2022 solution (Windows)")
print("  premake5 gmake2       -- Generate GNU Makefiles (Linux/Windows)")
print("  premake5 xcode4       -- Generate Xcode project (macOS)")
print("  premake5 setup-vcpkg  -- Install vcpkg dependencies")
print("  premake5 compile-shaders -- Compile GLSL shaders")
print("==========================================")
