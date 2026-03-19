file(GLOB_RECURSE TOOL_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_executable(NatureOfCraftCooker ${TOOL_FILES})
set(NOC_LUAJIT_RUNTIME_DLL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/lua51.dll")

target_include_directories(NatureOfCraftCooker PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_options(NatureOfCraftCooker PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++latest>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++2c>
)
target_link_libraries(NatureOfCraftCooker PRIVATE Source)

add_custom_command(TARGET NatureOfCraftCooker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NatureOfCraftCooker>/Content/Engine"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Editor/Resources"
            "$<TARGET_FILE_DIR:NatureOfCraftCooker>/Content/Engine"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NatureOfCraftCooker>/Content/Engine/NIS"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Source/ThirdParty/NIS"
            "$<TARGET_FILE_DIR:NatureOfCraftCooker>/Content/Engine/NIS"
    COMMENT "Copying engine content for NatureOfCraftCooker"
)

add_custom_command(TARGET NatureOfCraftCooker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_RUNTIME_DLLS:NatureOfCraftCooker>" "$<TARGET_FILE_DIR:NatureOfCraftCooker>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying NatureOfCraftCooker runtime DLLs"
)

add_custom_command(TARGET NatureOfCraftCooker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOC_LUAJIT_RUNTIME_DLL}" "$<TARGET_FILE_DIR:NatureOfCraftCooker>"
    COMMENT "Copying LuaJIT runtime for NatureOfCraftCooker"
)
