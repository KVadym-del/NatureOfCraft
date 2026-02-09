file(GLOB_RECURSE EDITOR_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_executable(Editor ${EDITOR_FILES})

target_include_directories(Editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_options(Editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++latest>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++2c>
)
target_link_libraries(Editor PRIVATE Source)

# --- Shader compilation ---

find_program(GLSLC_EXECUTABLE NAMES glslc
    HINTS
        ${Vulkan_BIN_DIR}
        "$ENV{VULKAN_SDK}/Bin"
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/shaderc"
)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Install Vulkan SDK or 'shaderc' via vcpkg.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/Resources")
file(MAKE_DIRECTORY "${SHADER_BINARY_DIR}")

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
        COMMENT "Compiling ${SHADER_TYPE} shader"
    )
    list(APPEND SPV_FILES ${SPV_OUTPUT})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPV_FILES})
add_dependencies(Editor CompileShaders)

# --- Copy runtime assets ---

add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Source> $<TARGET_FILE_DIR:Editor>
    COMMENT "Copying Source.dll to output directory"
)

file(GLOB MODEL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.obj")
foreach(MODEL ${MODEL_FILES})
    get_filename_component(MODEL_NAME ${MODEL} NAME)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MODEL} "$<TARGET_FILE_DIR:Editor>/Resources/${MODEL_NAME}"
        COMMENT "Copying ${MODEL_NAME}"
    )
endforeach()
