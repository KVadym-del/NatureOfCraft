cmake_minimum_required(VERSION 3.25)
project("NatureOfCraft" LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>")

add_subdirectory("Source")
add_subdirectory("Editor")
