if os.isfile("build_conan/conantoolchain.premake5.lua") then
    include "build_conan/conantoolchain.premake5.lua"
end
if os.isfile("build_conan/conandeps.premake5.lua") then
    include "build_conan/conandeps.premake5.lua"
end

workspace "NatureOfCraft"
architecture "x86_64"
startproject "NatureOfCraft"

configurations { "Debug", "Release" }

targetdir("bin/%{cfg.buildcfg}")
objdir("build/%{cfg.buildcfg}")

project "NatureOfCraft"
kind "ConsoleApp"
language "C++"
cppdialect "C++23"
staticruntime "off"

files {
    "src/main.cpp",
    "src/Window/Private/Window.cpp",
    "src/Rendering/BackEnds/Private/Vulkan.cpp",
}

prebuildcommands {
    "glslc src/Resources/shader.vert -o src/Resources/vert.spv",
    "glslc src/Resources/shader.frag -o src/Resources/frag.spv"
}

filter "configurations:Debug"
defines { "DEBUG" }
symbols "On"
optimize "Off"
buildoptions { "-Wall", "-Wextra" }

filter "configurations:Release"
defines { "NDEBUG" }
optimize "On"
buildoptions { "-Wall", "-Wextra" }

filter {}

filter "configurations:Debug"
if conan_setup then
    conan_setup("debug_x86_64")
end

filter "configurations:Release"
if conan_setup then
    conan_setup("release_x86_64")
end

filter {}
