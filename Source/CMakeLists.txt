find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(RapidObj CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(flatbuffers CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_path(SOL2_INCLUDE_DIRS "sol/abort.hpp")
find_path(LUAJIT_INCLUDE_DIR luajit.h PATH_SUFFIXES luajit)
find_library(LUAJIT_LIBRARY NAMES lua51)

# --- FlatBuffers schema compilation ---
find_program(FLATC_EXECUTABLE flatc
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/flatbuffers"
    REQUIRED
)

set(FBS_SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Schemas")
set(FBS_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Generated")

file(GLOB FBS_SCHEMAS "${FBS_SCHEMA_DIR}/*.fbs")

set(FBS_GENERATED_HEADERS "")
foreach(FBS_FILE ${FBS_SCHEMAS})
    get_filename_component(FBS_NAME ${FBS_FILE} NAME_WE)
    set(FBS_OUTPUT "${FBS_GENERATED_DIR}/${FBS_NAME}_generated.h")
    list(APPEND FBS_GENERATED_HEADERS ${FBS_OUTPUT})

    add_custom_command(
        OUTPUT ${FBS_OUTPUT}
        COMMAND ${FLATC_EXECUTABLE} --cpp -o "${FBS_GENERATED_DIR}" "${FBS_FILE}"
        DEPENDS ${FBS_FILE}
        COMMENT "Compiling FlatBuffers schema: ${FBS_NAME}.fbs"
    )
endforeach()

add_custom_target(FlatBufferSchemas DEPENDS ${FBS_GENERATED_HEADERS})

# --- Source files ---
file(GLOB_RECURSE SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(Source SHARED ${SOURCE_FILES})
add_dependencies(Source FlatBufferSchemas)

target_include_directories(Source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(Source PUBLIC "${FBS_GENERATED_DIR}")

target_include_directories(Source PRIVATE ${SOL2_INCLUDE_DIRS})
target_include_directories(Source PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(Source PRIVATE ${LUAJIT_INCLUDE_DIR})

target_compile_options(Source PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++latest>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++2c>
)

target_compile_definitions(Source PRIVATE SOL_LUAJIT=1)

target_link_libraries(Source PUBLIC
    fmt::fmt
    glfw
    Vulkan::Vulkan
    imgui::imgui
    rapidobj::rapidobj
    Microsoft::DirectXMath
    Taskflow::Taskflow
    EnTT::EnTT
    flatbuffers::flatbuffers
)

target_link_libraries(Source PRIVATE
    unofficial::shaderc::shaderc
    ${LUAJIT_LIBRARY}
)
