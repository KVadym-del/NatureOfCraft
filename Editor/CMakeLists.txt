set(MediaFiles "./Source/Media/VideoPlayer.hpp" "./Source/Media/AudioPlayer.hpp" "./Source/Media/MediaPlayer.hpp")
set(UIFiles "./Source/UI/ImGuiLayer.cpp" "./Source/UI/ImGuiLayer.hpp")
set(EditorFiles "./Source/main.cpp")

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

find_package(imgui CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(portaudio CONFIG REQUIRED)

find_program(GLSLC_EXECUTABLE NAMES glslc 
    HINTS 
        ${Vulkan_BIN_DIR} 
        "$ENV{VULKAN_SDK}/Bin"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/shaderc"
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/shaderc"
)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please install Vulkan SDK or 'shaderc' via vcpkg.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/Resources")
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

file(GLOB SHADER_FILES "${SHADER_SOURCE_DIR}/*.vert" "${SHADER_SOURCE_DIR}/*.frag")
set(SPV_FILES "")

foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_EXT ${SHADER} EXT)
    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_TYPE)
    set(SPV_OUTPUT "${SHADER_BINARY_DIR}/${SHADER_TYPE}.spv")
    
    add_custom_command(
        OUTPUT ${SPV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER} -> ${SPV_OUTPUT}.spv"
    )
    list(APPEND SPV_FILES ${SPV_OUTPUT})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPV_FILES})

add_executable(Editor 
	${MediaFiles} 
	${UIFiles} 
	${EditorFiles}
)

target_include_directories(Editor PRIVATE 
    ${CMAKE_SOURCE_DIR}/Source           
    ${CMAKE_SOURCE_DIR}/Editor/Source    
)

add_dependencies(Editor CompileShaders)

if (CMAKE_VERSION VERSION_GREATER 3.25)
  set_property(TARGET Editor PROPERTY CXX_STANDARD 26)
endif()

target_link_libraries(Editor PRIVATE Source)
target_link_libraries(Editor PRIVATE portaudio)
target_link_libraries(Editor PRIVATE swresample)

add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Source>
        $<TARGET_FILE_DIR:Editor>
    COMMENT "Copying Source.dll to Editor output directory"
)
