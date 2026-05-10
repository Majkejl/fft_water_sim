#pragma once

#include <webgpu/webgpu.hpp>

/* Convenience constructors for common WebGPU texture and texture-view patterns.
   Both functions create single-mip, single-sample resources. */
namespace texture_helpers {

/* Creates a 2D texture with the given dimensions, format, and usage flags. */
inline wgpu::Texture create_texture_2d(
    wgpu::Device        device,
    uint32_t            width,
    uint32_t            height,
    wgpu::TextureFormat  format,
    WGPUTextureUsageFlags usage)
{
    wgpu::TextureDescriptor d;
    d.dimension       = wgpu::TextureDimension::_2D;
    d.size            = { width, height, 1 };
    d.mipLevelCount   = 1;
    d.sampleCount     = 1;
    d.format          = format;
    d.usage           = usage;
    d.viewFormatCount = 0;
    d.viewFormats     = nullptr;
    return device.createTexture(d);
}

/* Creates a 2D texture view covering the entire texture (all layers, all mips). */
inline wgpu::TextureView create_view_2d(wgpu::Texture texture, wgpu::TextureFormat format)
{
    wgpu::TextureViewDescriptor d;
    d.aspect          = wgpu::TextureAspect::All;
    d.baseArrayLayer  = 0;
    d.arrayLayerCount = 1;
    d.baseMipLevel    = 0;
    d.mipLevelCount   = 1;
    d.dimension       = wgpu::TextureViewDimension::_2D;
    d.format          = format;
    return texture.createView(d);
}

} // namespace texture_helpers
