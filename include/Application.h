#pragma once

#include "webgpu/webgpu.hpp"
#include "webgpu-utils.h"
#include "Camera.h"
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif


/* Render-side uniforms uploaded to the GPU each frame.
   Layout must exactly match the MyUniforms struct in water.wgsl and skybox.wgsl. */
struct MyUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 eye_pos;
    float     N;           /* texture resolution (TEXTURE_SIZE) as float */
    float     patch_size;  /* physical ocean patch width in metres */
    float     lambda;      /* choppiness / Jacobian scale */
    float     _pad0;
    float     _pad1;
};

/* Per-dispatch compute uniforms written into the dynamic-offset uniform buffer.
   Layout must exactly match c_Uniforms in fft.wgsl and time_spectrum.wgsl. */
struct ComputeUniforms {
    float    time;
    uint32_t stage;
    uint32_t N;
    uint32_t log2n;
};

/* Per-frame uniforms for the foam compute shader. */
struct FoamUniforms {
    float lambda;
    float threshold;  /* J breaking point — biasedJ = max(0, -(J - threshold)) */
    float erosion;
    float fft_n;      /* TEXTURE_SIZE — normalisation divisor (N²) */
    float foam_add;   /* proportional accumulation scale */
    float _pad0, _pad1, _pad2;
};


/* Top-level application class.
   Owns the WebGPU device, all GPU resources, and drives the main render/compute loop. */
class Application
{
    // --- CPU-side render uniforms ---
    MyUniforms uniforms;

    // --- window / WebGPU core ---
    uint32_t    width, height;
    GLFWwindow* window;
    wgpu::Device  device;
    wgpu::Queue   queue;
    wgpu::Surface surface;
    std::unique_ptr<wgpu::ErrorCallback> error_callback;
    wgpu::TextureFormat surface_format = wgpu::TextureFormat::Undefined;

    // --- depth buffer ---
    wgpu::Texture     depth_texture;
    wgpu::TextureView depth_texture_view;

    // --- render pipelines ---
    wgpu::RenderPipeline pipeline;         /* water surface */
    wgpu::RenderPipeline skybox_pipeline;  /* fullscreen skybox */

    // --- compute pipelines ---
    wgpu::ComputePipeline time_spectrum_pipeline; /* h0(k) → h(k,t) + displacement spectra */
    wgpu::ComputePipeline fft_h_pipeline;         /* row-wise IFFT stage */
    wgpu::ComputePipeline fft_v_pipeline;         /* column-wise IFFT stage */
    wgpu::ComputePipeline foam_pipeline;          /* Jacobian-based foam accumulation */

    // --- geometry buffers ---
    wgpu::Buffer vertex_buffer;
    wgpu::Buffer index_buffer;
    uint32_t     index_count;

    // --- uniform buffers ---
    wgpu::Buffer uniform_buffer;          /* render-side MyUniforms */
    wgpu::Buffer compute_uniform_buffer;  /* ComputeUniforms × TEXTURE_LOG slots */
    uint32_t     compute_uniform_stride;  /* 256-byte aligned stride for dynamic offsets */
    wgpu::Buffer foam_uniform_buffer;     /* FoamUniforms (32 bytes, written each frame) */

    // --- render-side bind group ---
    wgpu::BindGroup       bind_group;
    wgpu::PipelineLayout  pipeline_layout;
    wgpu::BindGroupLayout bind_group_layout;

    // --- time_spectrum compute bind group (one per frame, no ping-pong) ---
    wgpu::BindGroup        time_spectrum_bind_group;
    wgpu::BindGroupLayout  time_spectrum_bgl;
    wgpu::PipelineLayout   time_spectrum_layout;

    // --- FFT compute bind groups (ping-pong pairs for all 5 IFFT channels) ---
    /* [0]: out=tex[0], in=tex[1]   [1]: out=tex[1], in=tex[0] */
    wgpu::BindGroup       h_fft_bind_groups[2];
    wgpu::BindGroup       sx_fft_bind_groups[2];
    wgpu::BindGroup       sy_fft_bind_groups[2];
    wgpu::BindGroup       dx_fft_bind_groups[2];
    wgpu::BindGroup       dy_fft_bind_groups[2];
    wgpu::BindGroupLayout fft_bgl;
    wgpu::PipelineLayout  fft_layout;

    // --- foam bind groups (ping-pong per frame) ---
    /* [i]: reads foam_textures[i], writes foam_textures[1-i] */
    wgpu::BindGroup       foam_bind_groups[2];
    wgpu::BindGroupLayout foam_bgl;
    wgpu::PipelineLayout  foam_layout;
    uint32_t              foam_frame = 0;

    // --- ocean simulation textures ---
    wgpu::Texture     height_textures[2];      /* ping-pong IFFT targets (RGBA32Float) */
    wgpu::TextureView height_texture_views[2];
    wgpu::Texture     slope_x_textures[2];     /* ping-pong for ∂h/∂x (RGBA32Float) */
    wgpu::TextureView slope_x_texture_views[2];
    wgpu::Texture     slope_y_textures[2];     /* ping-pong for ∂h/∂y (RGBA32Float) */
    wgpu::TextureView slope_y_texture_views[2];
    wgpu::Texture     disp_x_textures[2];      /* ping-pong for Dx choppy displacement */
    wgpu::TextureView disp_x_texture_views[2];
    wgpu::Texture     disp_y_textures[2];      /* ping-pong for Dy choppy displacement */
    wgpu::TextureView disp_y_texture_views[2];
    wgpu::Texture     foam_textures[2];        /* foam accumulation ping-pong (R32Float) */
    wgpu::TextureView foam_texture_views[2];
    wgpu::Texture     spectrum_texture;        /* initial h0(k) spectrum (RGBA32Float) */
    wgpu::TextureView spectrum_texture_view;
    wgpu::Texture     butterfly_texture;       /* precomputed DIT butterfly table */
    wgpu::TextureView butterfly_texture_view;
    wgpu::Texture     k_data_texture;          /* wave-vector kx/ky and dispersion omega */
    wgpu::TextureView k_data_texture_view;

    // --- foam detail texture ---
    wgpu::Texture     foam_detail_texture;
    wgpu::TextureView foam_detail_texture_view;

    // --- environment / skybox ---
    wgpu::Texture     cubemap_texture;
    wgpu::TextureView cubemap_texture_view;
    std::vector<std::string> cubemap_paths; /* sorted list of PNG paths found at startup */
    int cubemap_index = 0;

    // --- shared sampler ---
    wgpu::Sampler sampler;

    // --- orbit camera ---
    Camera camera;
    bool   mouse_dragging = false;
    double last_mouse_x   = 0.0;
    double last_mouse_y   = 0.0;

    // --- ImGui UI panels (each lambda renders one panel window) ---
    std::vector<std::function<void()>> ui_panels;

private:
    wgpu::TextureView    get_next_surface_view();
    wgpu::RequiredLimits get_required_limits(wgpu::Adapter adapter) const;

    void init_pipeline();
    void init_compute();
    void init_buffers();
    void init_bind_groups();
    void init_textures();
    void init_cubemap();
    void rebuild_render_bind_group(int foam_idx = 0);
    void run_compute();
    void render_ui(wgpu::RenderPassEncoder pass);

    /* GLFW input callbacks — registered in the constructor via glfwSetWindowUserPointer. */
    static void on_mouse_button(GLFWwindow* w, int button, int action, int mods);
    static void on_cursor_pos(GLFWwindow* w, double x, double y);
    static void on_scroll(GLFWwindow* w, double dx, double dy);

public:
    Application(int width, int height);
    ~Application();

    /* Executes one frame: compute IFFT pass → render pass → present. */
    void main_loop();

    /* Loads the cubemap at the given index from the sorted list discovered at startup. */
    void load_cubemap(int index);

    /* Returns false when the OS window-close event has been received. */
    bool is_running();
};
