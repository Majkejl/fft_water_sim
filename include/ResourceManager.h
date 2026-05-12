#pragma once

#include <vector>
#include <cstdint>
#include <filesystem>
#include <webgpu/webgpu.hpp>

/* Static utility class for loading files and GPU resources from disk. */
class ResourceManager {
public:
    /* Loads a WGSL source file and compiles it into a shader module on the given device.
       Returns nullptr on failure (file not found or compilation error). */
    static wgpu::ShaderModule load_shader_module(
        const std::filesystem::path& path,
        wgpu::Device device);

    /* Loads a horizontal-cross cubemap PNG and extracts the six faces into facePixels.
       Faces are ordered in WebGPU layer order: +X(0), -X(1), +Y(2), -Y(3), +Z(4), -Z(5).
       The direction-to-face mapping for Z-up worlds is handled in the shaders.
       Returns false and leaves facePixels unchanged on failure. */
    static bool load_cubemap_cross(
        const std::filesystem::path& path,
        std::vector<uint8_t>&        face_pixels,
        int&                         face_size);

    /* Loads any image format supported by stb_image (PNG, JPG, …) as RGBA8.
       Returns false on failure; width and height are set on success. */
    static bool load_image(
        const std::filesystem::path& path,
        std::vector<uint8_t>&        pixels,
        int&                         width,
        int&                         height);
};
