if os.isfile("build_conan/conantoolchain.premake5.lua") then
    include "build_conan/conantoolchain.premake5.lua"
end
if os.isfile("build_conan/conandeps.premake5.lua") then
    include "build_conan/conandeps.premake5.lua"
end

workspace "NatureOfCraft"
architecture "x86_64"
startproject "NatureOfCraftEditor"

configurations { "Debug", "Release" }

targetdir("bin/%{cfg.buildcfg}")
objdir("build/%{cfg.buildcfg}")

project "NatureOfCraft"
kind "SharedLib"
language "C++"
cppdialect "C++23"
staticruntime "off"

-- Engine sources (exclude the app entry point)
files {
    "Source/Window/Private/Window.cpp",
    "Source/Rendering/BackEnds/Private/Vulkan.cpp",
}

includedirs { "Source" }

-- Compile shaders before building the engine
prebuildcommands {
    "glslc Editor/Resources/shader.vert -o Editor/Resources/vert.spv",
    "glslc Editor/Resources/shader.frag -o Editor/Resources/frag.spv"
}

filter "system:linux"
pic "On"
filter {}

filter "configurations:Debug"
defines { "DEBUG" }
symbols "On"
optimize "Off"
buildoptions { "-Wall", "-Wextra" }
filter {}

filter "configurations:Release"
defines { "NDEBUG" }
optimize "On"
buildoptions { "-Wall", "-Wextra" }
filter {}
filter {}

-- Apply Conan dependency setup for NatureOfCraft
filter "configurations:Debug"
if conan_setup then
    conan_setup("debug_x86_64")
end

filter "configurations:Release"
if conan_setup then
    conan_setup("release_x86_64")
end

filter {}

project "NatureOfCraftEditor"
kind "ConsoleApp"
language "C++"
cppdialect "C++23"
staticruntime "off"

files {
    "Editor/Source/main.cpp",
    "Editor/Source/UI/ImGuiLayer.cpp"
}
includedirs { "Source", "Editor/Source" }

-- Link against the engine shared library
links { "NatureOfCraft" }
dependson { "NatureOfCraft" }

-- Ensure linker can find the built shared library
libdirs { "bin/%{cfg.buildcfg}" }

-- Let the executable find the shared library at runtime on Linux
filter "system:linux"
linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
filter {}

filter "configurations:Debug"
defines { "DEBUG" }
symbols "On"
optimize "Off"
buildoptions { "-Wall", "-Wextra" }
filter {}

filter "configurations:Release"
defines { "NDEBUG" }
optimize "On"
buildoptions { "-Wall", "-Wextra" }
filter {}
filter {}

-- Apply Conan dependency setup for NatureOfCraftEditor
filter "configurations:Debug"
if conan_setup then
    conan_setup("debug_x86_64")
end

filter "configurations:Release"
if conan_setup then
    conan_setup("release_x86_64")
end

filter {}
