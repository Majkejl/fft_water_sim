cmake_minimum_required(VERSION 3.0...3.25)
project(
    fft_water_sim
    VERSION 0.1.0 
    LANGUAGES CXX C
)


#add_subdirectory(source)
add_subdirectory(webgpu)

add_executable(fft_water_sim main.cpp)

target_link_libraries(fft_water_sim PRIVATE webgpu)

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

# Options that are specific to Emscripten
if (EMSCRIPTEN)
	# Generate a full web page rather than a simple WebAssembly module
	set_target_properties(fft_water_sim PROPERTIES SUFFIX ".html")
    # Enable the use of emscripten_sleep()
    target_link_options(fft_water_sim PRIVATE -sASYNCIFY)
endif()
