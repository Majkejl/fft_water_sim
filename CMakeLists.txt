cmake_minimum_required(VERSION 3.0...3.25)
project(
    fft_water_sim
    VERSION 0.1.0 
    LANGUAGES CXX C
)
    
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
    include/PerlinNoise.hpp
    include/Application.h
    include/webgpu-utils.h
    include/ResourceManager.h
    src/Application.cpp
    src/webgpu-utils.cpp  
    src/ResourceManager.cpp
)

target_include_directories(fft_water_sim
     PUBLIC include
)

target_link_libraries(fft_water_sim PRIVATE webgpu glfw glfw3webgpu glm)

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
        -sASYNCIFY
        --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )
endif()


