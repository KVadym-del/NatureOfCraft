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

# --- Copy shader source files to output (compiled at runtime by ShaderCompiler) ---

file(GLOB SHADER_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.frag"
)
foreach(SHADER_SRC ${SHADER_SOURCE_FILES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_SRC} "$<TARGET_FILE_DIR:Editor>/Resources/${SHADER_NAME}"
        COMMENT "Copying shader source: ${SHADER_NAME}"
    )
endforeach()

# --- Copy NIS (NVIDIA Image Scaling) shader files to output ---

file(GLOB NIS_SHADER_FILES
    "${CMAKE_SOURCE_DIR}/Source/ThirdParty/NIS/*.glsl"
    "${CMAKE_SOURCE_DIR}/Source/ThirdParty/NIS/*.h"
)
foreach(NIS_SRC ${NIS_SHADER_FILES})
    get_filename_component(NIS_NAME ${NIS_SRC} NAME)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Editor>/Resources/NIS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NIS_SRC} "$<TARGET_FILE_DIR:Editor>/Resources/NIS/${NIS_NAME}"
        COMMENT "Copying NIS shader: ${NIS_NAME}"
    )
endforeach()

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

# Copy material and texture assets
file(GLOB ASSET_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.mtl"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.jpeg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.tga"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*.bmp"
)
foreach(ASSET ${ASSET_FILES})
    get_filename_component(ASSET_NAME ${ASSET} NAME)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASSET} "$<TARGET_FILE_DIR:Editor>/Resources/${ASSET_NAME}"
        COMMENT "Copying ${ASSET_NAME}"
    )
endforeach()

# --- Copy Lua scripts (preserving subdirectory structure) ---

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/scripts")
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Resources/scripts"
            "$<TARGET_FILE_DIR:Editor>/Resources/scripts"
        COMMENT "Copying Lua scripts"
    )
endif()

