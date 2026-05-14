cmake_minimum_required(VERSION 3.0...3.25)
project(
    fft_water_sim
    VERSION 0.1.0
    LANGUAGES CXX C
)

include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    UPDATE_DISCONNECTED TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw webgpu)
    
if (NOT EMSCRIPTEN)
    add_subdirectory(glfw)
else()
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()
    
add_subdirectory(webgpu)
add_subdirectory(glfw3webgpu)
add_subdirectory(glm)


add_executable(fft_water_sim
    main.cpp
    include/Application.h
    include/Camera.h
    include/OceanSim.h
    include/Renderer.h
    include/SimulationConfig.h
    include/Pipelines.h
    include/Textures.h
    include/ResourceManager.h
    include/webgpu-utils.h
    src/Application.cpp
    src/Camera.cpp
    src/OceanSim.cpp
    src/Renderer.cpp
    src/ResourceManager.cpp
    src/webgpu-utils.cpp
)

target_include_directories(fft_water_sim
    PUBLIC include
    PUBLIC ${stb_SOURCE_DIR}
)



target_link_libraries(fft_water_sim PRIVATE webgpu glfw glfw3webgpu glm imgui_lib)

# We add an option to enable different settings when developing the app than
# when distributing it.
option(DEV_MODE "Set up development helper settings" ON)

if(DEV_MODE)
    # In dev mode, we load resources from the source tree, so that when we
    # dynamically edit resources (like shaders), these are correctly
    # versionned.
    target_compile_definitions(fft_water_sim PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
else()
    # In release mode, we just load resources relatively to wherever the
    # executable is launched from, so that the binary is portable
    target_compile_definitions(fft_water_sim PRIVATE
        RESOURCE_DIR="./resources"
    )
endif()

target_copy_webgpu_binaries(fft_water_sim)


set_target_properties(fft_water_sim PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
	target_compile_options(fft_water_sim PRIVATE /W4)
else()
	target_compile_options(fft_water_sim PRIVATE -Wall -Wextra -pedantic)
endif()

if (XCODE)
	set_target_properties(fft_water_sim PROPERTIES
		XCODE_GENERATE_SCHEME ON
		XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
	)
endif()

if (EMSCRIPTEN)
	set_target_properties(fft_water_sim PROPERTIES SUFFIX ".html")
    target_link_options(fft_water_sim PRIVATE
        -sALLOW_MEMORY_GROWTH
        -sASYNCIFY
        --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
endif()


