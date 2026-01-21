set(CoreFiles "./Core/Public/Core.hpp" "./Core/Public/Expected.hpp" "./Core/Public/Utils.hpp") 
set(RenderingFiles "./Rendering/BackEnds/Private/Vulkan.cpp" "./Rendering/BackEnds/Public/Vulkan.hpp") 
set(WindowFiles "./Window/Private/Window.cpp" "./Window/Public/Window.hpp") 

if(MSVC)
    add_compile_options("/utf-8")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_library (Source 
	SHARED
	${CoreFiles} 
	${RenderingFiles} 
	${WindowFiles}
)

if (CMAKE_VERSION VERSION_GREATER 3.25)
  set_property(TARGET Source PROPERTY CXX_STANDARD 26)
endif()

target_link_libraries(Source PUBLIC fmt::fmt)
target_link_libraries(Source PUBLIC glfw)
target_link_libraries(Source PUBLIC Vulkan::Headers)
target_link_libraries(Source PUBLIC Vulkan::Vulkan)
target_link_libraries(Source PUBLIC imgui::imgui)

