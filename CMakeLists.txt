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


add_executable(fft_water_sim
main.cpp 
include/Application.h
include/webgpu-utils.h
src/Application.cpp
src/webgpu-utils.cpp  
)

target_include_directories(fft_water_sim
     PUBLIC include
)

target_link_libraries(fft_water_sim PRIVATE webgpu glfw glfw3webgpu)

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
    target_link_options(fft_water_sim PRIVATE -sASYNCIFY)
endif()


