#pragma once

#include "webgpu/webgpu.hpp"
#include "OceanSim.h"
#include "SimulationConfig.h"
#include "Pipelines.h"
#include "Textures.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <string>
#include <vector>

/* Render-side uniforms uploaded to the GPU each frame.
   Layout must exactly match MyUniforms in water.wgsl and skybox.wgsl. */
struct RenderUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 eye_pos;
    float     N;
    float     patch_size;
    float     lambda;
    float     _pad0;
    float     _pad1;
};

/* Owns all render-side GPU resources: pipelines, geometry, cubemap, depth texture,
   foam detail texture, sampler, and the render bind group. */
class Renderer {
    wgpu::Device      device;
    wgpu::Queue       queue;
    wgpu::TextureFormat surface_format = wgpu::TextureFormat::Undefined;
    uint32_t          width  = 0;
    uint32_t          height = 0;

    // --- render pipelines ---
    wgpu::RenderPipeline pipeline;
    wgpu::RenderPipeline skybox_pipeline;

    // --- geometry ---
    wgpu::Buffer vertex_buffer;
    wgpu::Buffer index_buffer;
    uint32_t     index_count = 0;

    // --- uniform buffer ---
    wgpu::Buffer uniform_buffer;

    // --- bind group ---
    wgpu::BindGroup       bind_group;
    wgpu::PipelineLayout  pipeline_layout;
    wgpu::BindGroupLayout bind_group_layout;

    // --- depth buffer ---
    wgpu::Texture     depth_texture;
    wgpu::TextureView depth_texture_view;

    // --- environment cubemap ---
    wgpu::Texture            cubemap_texture;
    wgpu::TextureView        cubemap_texture_view;
    std::vector<std::string> cubemap_paths;
    int                      cubemap_index = 0;

    // --- foam detail ---
    wgpu::Texture     foam_detail_texture;
    wgpu::TextureView foam_detail_texture_view;

    // --- shared sampler ---
    wgpu::Sampler sampler;

    void init_pipelines();
    void init_geometry();
    void init_depth();
    void init_foam_detail();
    void init_sampler();
    void load_cubemap_texture(int index);

public:
    Renderer() = default;
    ~Renderer();

    /* Allocates all render-side GPU resources. Call once after the device is created. */
    void init(wgpu::Device d, wgpu::Queue q, wgpu::TextureFormat fmt,
              uint32_t w, uint32_t h, const SimulationConfig& config);

    /* (Re)builds the render bind group pointing to the current OceanSim textures.
       foam_idx is the index of the foam texture most recently written by OceanSim::tick(). */
    void rebuild_bind_group(const OceanSim& ocean, int foam_idx);

    /* Uploads uniforms to the GPU, then issues the water mesh and skybox draw calls. */
    void draw(wgpu::RenderPassEncoder pass, const RenderUniforms& uniforms);

    /* Scans RESOURCE_DIR/Cubemap/ for PNGs and loads the one at the given index. */
    void init_cubemap(const SimulationConfig& config);

    /* Loads a different cubemap and rebuilds the render bind group. */
    void load_cubemap(int index, const OceanSim& ocean, int foam_idx);

    wgpu::TextureView depth_view() const { return depth_texture_view; }
};
