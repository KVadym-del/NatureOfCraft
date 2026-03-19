file(GLOB_RECURSE GAME_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_executable(Game ${GAME_FILES})

target_include_directories(Game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_options(Game PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++latest>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++2c>
)
target_link_libraries(Game PRIVATE Source)

set(NOC_COOK_PROJECT "" CACHE FILEPATH "Path to the .noc_project file to cook for Game packaging")
set(NOC_COOKED_CONTENT_DIR "${CMAKE_BINARY_DIR}/cooked-content" CACHE PATH "Cooked game content output directory")
set(NOC_LUAJIT_RUNTIME_DLL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/lua51.dll")

add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/Content/Engine"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Editor/Resources"
            "$<TARGET_FILE_DIR:Game>/Content/Engine"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/Content/Engine/NIS"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Source/ThirdParty/NIS"
            "$<TARGET_FILE_DIR:Game>/Content/Engine/NIS"
    COMMENT "Copying engine content for Game"
)

add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_RUNTIME_DLLS:Game>" "$<TARGET_FILE_DIR:Game>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying Game runtime DLLs"
)

add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOC_LUAJIT_RUNTIME_DLL}" "$<TARGET_FILE_DIR:Game>"
    COMMENT "Copying LuaJIT runtime for Game"
)

if(TARGET NatureOfCraftCooker)
    if(NOT NOC_COOK_PROJECT STREQUAL "")
        add_custom_target(CookGameContent
            COMMAND "$<TARGET_FILE:NatureOfCraftCooker>"
                --project "${NOC_COOK_PROJECT}"
                --output "${NOC_COOKED_CONTENT_DIR}"
            DEPENDS NatureOfCraftCooker
            COMMENT "Cooking game content into ${NOC_COOKED_CONTENT_DIR}"
            VERBATIM
        )
    else()
        add_custom_target(CookGameContent
            COMMAND ${CMAKE_COMMAND} -E echo "Set NOC_COOK_PROJECT to cook project content."
            COMMENT "CookGameContent skipped"
        )
    endif()
else()
    add_custom_target(CookGameContent
        COMMAND ${CMAKE_COMMAND} -E echo "NatureOfCraftCooker target is unavailable."
        COMMENT "CookGameContent unavailable"
    )
endif()

if(TARGET NatureOfCraftCooker)
    if(NOT NOC_COOK_PROJECT STREQUAL "")
        add_custom_target(CreateFinalBundle
            COMMAND "$<TARGET_FILE:NatureOfCraftCooker>"
                --project "${NOC_COOK_PROJECT}"
                --bundle-output "${CMAKE_BINARY_DIR}/bundle/$<CONFIG>"
                --runtime-dir "$<TARGET_FILE_DIR:Game>"
            DEPENDS Game NatureOfCraftCooker
            COMMENT "Creating the final standalone game bundle"
            VERBATIM
        )
    else()
        add_custom_target(CreateFinalBundle
            COMMAND ${CMAKE_COMMAND} -E echo "Set NOC_COOK_PROJECT to create a final game bundle."
            COMMENT "CreateFinalBundle skipped"
        )
    endif()
else()
    add_custom_target(CreateFinalBundle
        COMMAND ${CMAKE_COMMAND} -E echo "NatureOfCraftCooker target is unavailable."
        COMMENT "CreateFinalBundle unavailable"
    )
endif()

install(TARGETS Game RUNTIME DESTINATION ".")
install(FILES "$<TARGET_RUNTIME_DLLS:Game>" DESTINATION "." OPTIONAL)
install(FILES "${NOC_LUAJIT_RUNTIME_DLL}" DESTINATION "." OPTIONAL)
install(DIRECTORY "${NOC_COOKED_CONTENT_DIR}/Engine/" DESTINATION "Content/Engine" OPTIONAL)
install(DIRECTORY "${NOC_COOKED_CONTENT_DIR}/Game/" DESTINATION "Content/Game" OPTIONAL)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.txt" DESTINATION ".")

if(MSVC)
    install(FILES "$<TARGET_PDB_FILE:Game>" DESTINATION "symbols" OPTIONAL)
endif()

add_custom_target(PackageGame
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config "$<CONFIG>"
            --prefix "${CMAKE_BINARY_DIR}/package/$<CONFIG>"
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake" -C "$<CONFIG>"
    DEPENDS Game CookGameContent
    COMMENT "Installing and archiving the Game bundle"
    VERBATIM
)

if(WIN32)
    if(NOT NOC_COOK_PROJECT STREQUAL "")
        get_filename_component(NOC_COOK_PROJECT_FILENAME "${NOC_COOK_PROJECT}" NAME)
        add_custom_target(ValidateShippingBundle
            COMMAND "${CMAKE_BINARY_DIR}/bundle/$<CONFIG>/Game.exe"
                --content-root "${CMAKE_BINARY_DIR}/bundle/$<CONFIG>/Content"
                --project "${NOC_COOK_PROJECT_FILENAME}"
                --validate-startup
            DEPENDS CreateFinalBundle
            COMMENT "Validating cold start and first level load from the packaged Game bundle"
            VERBATIM
        )
    else()
        add_custom_target(ValidateShippingBundle
            COMMAND ${CMAKE_COMMAND} -E echo "Set NOC_COOK_PROJECT to validate a packaged project bundle."
            DEPENDS CreateFinalBundle
            COMMENT "ValidateShippingBundle skipped"
        )
    endif()
endif()
