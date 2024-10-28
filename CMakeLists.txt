cmake_minimum_required(VERSION 3.0...3.25)
project(
    fft_water_sim
    VERSION 0.1.0 
    LANGUAGES CXX C
)

add_subdirectory(source)

set_target_properties(fft_water_sim PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

target_compile_options(fft_water_sim PRIVATE /W4)

message(STATUS "Using Dawn backend for WebGPU")

add_library(webgpu INTERFACE)

if (EMSCRIPTEN)

	target_include_directories(webgpu INTERFACE
		"${CMAKE_CURRENT_SOURCE_DIR}/include-emscripten"
	)

	# This is used to advertise the flavor of WebGPU that this zip provides
	target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_EMSCRIPTEN)

	target_link_options(webgpu INTERFACE
		-sUSE_WEBGPU # Handle WebGPU symbols
	)

else (EMSCRIPTEN)

	include(cmake/FetchDawn.cmake)

	target_link_libraries(webgpu INTERFACE webgpu_dawn)
	target_include_directories(webgpu INTERFACE
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${CMAKE_BINARY_DIR}/_deps/dawn-src/include"
	)

	# This is used to advertise the flavor of WebGPU that this zip provides
	target_compile_definitions(webgpu INTERFACE WEBGPU_BACKEND_DAWN)

endif (EMSCRIPTEN)

# Does nothing, as this dawn-based distribution of WebGPU is statically linked
function(target_copy_webgpu_binaries Target)
endfunction()