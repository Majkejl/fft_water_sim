#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

#ifdef _MSC_VER
#  pragma warning(push, 0)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

using namespace wgpu;

wgpu::ShaderModule ResourceManager::load_shader_module(
    const std::filesystem::path& path, Device device)
{
    std::ifstream file(path);
    if (!file.is_open()) return nullptr;

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    std::string source(size, ' ');
    file.seekg(0);
    file.read(source.data(), size);

    ShaderModuleWGSLDescriptor wgsl_desc{};
    wgsl_desc.chain.next  = nullptr;
    wgsl_desc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    wgsl_desc.code        = source.c_str();

    ShaderModuleDescriptor desc{};
#ifdef WEBGPU_BACKEND_WGPU
    desc.hintCount = 0;
    desc.hints     = nullptr;
#endif
    desc.nextInChain = &wgsl_desc.chain;
    return device.createShaderModule(desc);
}

bool ResourceManager::load_cubemap_cross(
    const std::filesystem::path& path,
    std::vector<uint8_t>&        face_pixels,
    int&                         face_size)
{
    int w, h, ch;
    stbi_uc* img = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
    if (!img) return false;

    // Horizontal cross layout: 4 columns × 3 rows.
    //        [ +Y ]
    //  [-X] [+Z] [+X] [-Z]
    //        [ -Y ]
    face_size      = w / 4;
    const int yPad = (h - 3 * face_size) / 2;

    // WebGPU layer order: +X(0), -X(1), +Y(2), -Y(3), +Z(4), -Z(5).
    // Direction-to-face remapping for Z-up worlds is done in the shaders.
    const int face_x[6] = { 2*face_size, 0,          face_size,          face_size,          face_size,  3*face_size };
    const int face_y[6] = { yPad+face_size, yPad+face_size, yPad, yPad+2*face_size, yPad+face_size, yPad+face_size };

    const int bytes_per_face = face_size * face_size * 4;
    face_pixels.resize(6 * bytes_per_face);

    for (int face = 0; face < 6; face++) {
        uint8_t* dst = face_pixels.data() + face * bytes_per_face;
        for (int row = 0; row < face_size; row++) {
            const stbi_uc* src = img + ((face_y[face] + row) * w + face_x[face]) * 4;
            std::memcpy(dst + row * face_size * 4, src, static_cast<size_t>(face_size) * 4);
        }
    }

    stbi_image_free(img);
    return true;
}

bool ResourceManager::load_image(
    const std::filesystem::path& path,
    std::vector<uint8_t>&        pixels,
    int&                         width,
    int&                         height)
{
    int ch;
    stbi_uc* img = stbi_load(path.string().c_str(), &width, &height, &ch, 4);
    if (!img) return false;
    pixels.assign(img, img + width * height * 4);
    stbi_image_free(img);
    return true;
}
