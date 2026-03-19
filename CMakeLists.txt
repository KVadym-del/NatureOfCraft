cmake_minimum_required(VERSION 3.25)
project("NatureOfCraft" LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>")

if(MSVC)
    string(APPEND CMAKE_C_FLAGS_RELEASE " /O2 /GL")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /GL")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /LTCG /OPT:REF /OPT:ICF")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT NOC_IPO_SUPPORTED OUTPUT NOC_IPO_ERROR)
if(NOC_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "NatureOfCraft")
set(CPACK_PACKAGE_VENDOR "NatureOfCraft")
set(CPACK_PACKAGE_FILE_NAME "NatureOfCraft-${CMAKE_SYSTEM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

add_subdirectory("Source")
add_subdirectory("Editor")
add_subdirectory("Tools")
add_subdirectory("Game")

include(CPack)
