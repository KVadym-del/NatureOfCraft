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

# ── Self-contained release bundle ──────────────────────────────────────
# Usage: cmake --build <build-dir> --target Bundle
# Output: <build-dir>/bundle/NatureOfCraft/

set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/bundle/NatureOfCraft")

add_custom_target(Bundle
    DEPENDS Editor
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${BUNDLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}/Resources"
    # Executable
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Editor>" "${BUNDLE_DIR}/"
    # Runtime DLLs (Source.dll + vcpkg shared libs: fmt, glfw3, etc.)
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_RUNTIME_DLLS:Editor>" "${BUNDLE_DIR}/"
    # Vulkan loader — FindVulkan creates an import-lib target so
    # TARGET_RUNTIME_DLLS cannot resolve the DLL; copy it explicitly.
    COMMAND ${CMAKE_COMMAND} -E copy
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/vulkan-1.dll"
        "${BUNDLE_DIR}/"
    # Shader sources + sample assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "${BUNDLE_DIR}/Resources"
    COMMAND_EXPAND_LISTS
    COMMENT "Bundling self-contained release → ${BUNDLE_DIR}"
)
