find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(RapidObj CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(Source SHARED ${SOURCE_FILES})

target_include_directories(Source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_options(Source PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++latest>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++2c>
)

target_link_libraries(Source PUBLIC
    fmt::fmt
    glfw
    Vulkan::Vulkan
    imgui::imgui
    rapidobj::rapidobj
    Microsoft::DirectXMath
)
