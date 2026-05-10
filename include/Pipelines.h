#pragma once

#include <webgpu/webgpu.hpp>

/* Convenience constructors for wgpu::BindGroupLayoutEntry.
   Each function returns a fully-initialised entry for the given binding slot,
   shader visibility, and resource type. All other fields remain at their defaults. */
namespace pipeline_helpers {

/* Uniform buffer — optionally with dynamic offset for multi-stage dispatches. */
inline wgpu::BindGroupLayoutEntry uniform_layout(
    uint32_t             binding,
    WGPUShaderStageFlags visibility,
    bool                 dynamic_offset = false,
    uint64_t             min_size       = 0)
{
    wgpu::BindGroupLayoutEntry e    = wgpu::Default;
    e.binding                       = binding;
    e.visibility                    = visibility;
    e.buffer.type                   = wgpu::BufferBindingType::Uniform;
    e.buffer.hasDynamicOffset       = dynamic_offset;
    e.buffer.minBindingSize         = min_size;
    return e;
}

/* Sampled (read-only) texture. */
inline wgpu::BindGroupLayoutEntry texture_layout(
    uint32_t                   binding,
    WGPUShaderStageFlags       visibility,
    wgpu::TextureSampleType    sample_type = wgpu::TextureSampleType::Float,
    wgpu::TextureViewDimension dim         = wgpu::TextureViewDimension::_2D)
{
    wgpu::BindGroupLayoutEntry e = wgpu::Default;
    e.binding                    = binding;
    e.visibility                 = visibility;
    e.texture.sampleType         = sample_type;
    e.texture.viewDimension      = dim;
    return e;
}

/* Sampler. */
inline wgpu::BindGroupLayoutEntry sampler_layout(
    uint32_t                 binding,
    WGPUShaderStageFlags     visibility,
    wgpu::SamplerBindingType type = wgpu::SamplerBindingType::Filtering)
{
    wgpu::BindGroupLayoutEntry e = wgpu::Default;
    e.binding                    = binding;
    e.visibility                 = visibility;
    e.sampler.type               = type;
    return e;
}

/* Write-only storage texture (used as compute output). */
inline wgpu::BindGroupLayoutEntry storage_texture_layout(
    uint32_t             binding,
    WGPUShaderStageFlags visibility,
    wgpu::TextureFormat  format = wgpu::TextureFormat::RGBA32Float)
{
    wgpu::BindGroupLayoutEntry e       = wgpu::Default;
    e.binding                          = binding;
    e.visibility                       = visibility;
    e.storageTexture.access            = wgpu::StorageTextureAccess::WriteOnly;
    e.storageTexture.viewDimension     = wgpu::TextureViewDimension::_2D;
    e.storageTexture.format            = format;
    return e;
}

} // namespace pipeline_helpers
