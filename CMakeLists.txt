cmake_minimum_required(VERSION 3.0...3.25)
project(
    fft_water_sim
    VERSION 0.1.0 # any version number
    LANGUAGES CXX C # programming languages used by the project
)

add_subdirectory(source)

set_target_properties(fft_water_sim PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)

target_compile_options(fft_water_sim PRIVATE /W4)