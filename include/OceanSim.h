#pragma once

#include "webgpu/webgpu.hpp"
#include "SimulationConfig.h"
#include "Pipelines.h"
#include "Textures.h"
#include <cstdint>

/* Per-dispatch compute uniforms. Layout must match ComputeUniforms in fft.wgsl and time_spectrum.wgsl. */
struct FourierUniforms {
    float    time;
    uint32_t stage;
    uint32_t N;
    uint32_t log2n;
};

/* Per-frame foam compute uniforms. Layout must match FoamUniforms in foam.wgsl. */
struct FoamUniforms {
    float lambda;
    float threshold;
    float erosion;
    float fft_n;
    float foam_add;
    float _pad0, _pad1, _pad2;
};

/* Owns all GPU compute resources: pipelines, ping-pong textures, uniform buffers,
   and bind groups for the FFT ocean simulation. */
class OceanSim {
    wgpu::Device device;
    wgpu::Queue  queue;

    // --- compute pipelines ---
    wgpu::ComputePipeline time_spectrum_pipeline;
    wgpu::ComputePipeline fft_h_pipeline;
    wgpu::ComputePipeline fft_v_pipeline;
    wgpu::ComputePipeline foam_pipeline;

    // --- time_spectrum bind group (static — no ping-pong needed) ---
    wgpu::BindGroup       time_spectrum_bind_group;
    wgpu::BindGroupLayout time_spectrum_bgl;
    wgpu::PipelineLayout  time_spectrum_layout;

    // --- FFT bind groups (ping-pong pairs for 5 IFFT channels) ---
    wgpu::BindGroup       h_fft_bind_groups[2];
    wgpu::BindGroup       sx_fft_bind_groups[2];
    wgpu::BindGroup       sy_fft_bind_groups[2];
    wgpu::BindGroup       dx_fft_bind_groups[2];
    wgpu::BindGroup       dy_fft_bind_groups[2];
    wgpu::BindGroupLayout fft_bgl;
    wgpu::PipelineLayout  fft_layout;

    // --- foam bind groups (ping-pong per frame) ---
    wgpu::BindGroup       foam_bind_groups[2];
    wgpu::BindGroupLayout foam_bgl;
    wgpu::PipelineLayout  foam_layout;
    uint32_t              foam_frame = 0;

    // --- simulation textures ---
    wgpu::Texture     height_textures[2];
    wgpu::TextureView height_texture_views[2];
    wgpu::Texture     slope_x_textures[2];
    wgpu::TextureView slope_x_texture_views[2];
    wgpu::Texture     slope_y_textures[2];
    wgpu::TextureView slope_y_texture_views[2];
    wgpu::Texture     disp_x_textures[2];
    wgpu::TextureView disp_x_texture_views[2];
    wgpu::Texture     disp_y_textures[2];
    wgpu::TextureView disp_y_texture_views[2];
    wgpu::Texture     foam_textures[2];
    wgpu::TextureView foam_texture_views[2];
    wgpu::Texture     spectrum_texture;
    wgpu::TextureView spectrum_texture_view;
    wgpu::Texture     butterfly_texture;
    wgpu::TextureView butterfly_texture_view;
    wgpu::Texture     k_data_texture;
    wgpu::TextureView k_data_texture_view;

    // --- uniform buffers ---
    wgpu::Buffer compute_uniform_buffer;
    uint32_t     compute_uniform_stride = 0;
    wgpu::Buffer foam_uniform_buffer;

    void init_pipelines();
    void init_textures(const SimulationConfig& config);
    void init_buffers();
    void init_bind_groups();
    void upload_spectrum(const SimulationConfig& config);

public:
    OceanSim() = default;
    ~OceanSim();

    /* Allocates all GPU resources. Call once after the device is created. */
    void init(wgpu::Device d, wgpu::Queue q, const SimulationConfig& config);

    /* Dispatches one frame of compute work (time-spectrum + IFFT + foam).
       Returns the index of the foam texture just written — pass to Renderer::rebuild_bind_group. */
    int tick(float time, const SimulationConfig& config);

    /* Re-generates h0(k) spectrum from config (wind, amplitude, fetch).
       Call when any JONSWAP parameter changes. Does NOT advance the frame counter. */
    void rebuild_spectrum(const SimulationConfig& config);

    /* Texture view accessors for the Renderer to wire into its bind group. */
    wgpu::TextureView height_view()          const { return height_texture_views[0]; }
    wgpu::TextureView slope_x_view()         const { return slope_x_texture_views[0]; }
    wgpu::TextureView slope_y_view()         const { return slope_y_texture_views[0]; }
    wgpu::TextureView disp_x_view()          const { return disp_x_texture_views[0]; }
    wgpu::TextureView disp_y_view()          const { return disp_y_texture_views[0]; }
    wgpu::TextureView foam_view(int idx)     const { return foam_texture_views[idx]; }
};
