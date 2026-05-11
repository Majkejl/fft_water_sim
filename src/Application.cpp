
#define WEBGPU_CPP_IMPLEMENTATION

#include "Application.h"
#include "ResourceManager.h"
#include "Pipelines.h"
#include "Textures.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <cstring>
#include <numbers>
#include <filesystem>

using namespace wgpu;
using namespace pipeline_helpers;
using namespace texture_helpers;

static constexpr uint32_t   MESH_SIZE      = 256;
static constexpr uint32_t   TEXTURE_SIZE   = 256;
static constexpr uint32_t   FOAM_SIZE      = 1024;
static constexpr uint32_t   TEXTURE_LOG    = 8;
static constexpr float      PATCH_SIZE     = 64.f;
static constexpr double     WAVE_AMPLITUDE = 10.;

// ---------------------------------------------------------------------------
// Internal helpers (ocean data generation)
// ---------------------------------------------------------------------------
namespace {

void create_geometry(int size, std::vector<float>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            vertices.push_back(static_cast<float>(j) / (size - 1)); // x
            vertices.push_back(static_cast<float>(i) / (size - 1)); // y
            vertices.push_back(0.f);                                // z

            vertices.push_back(static_cast<float>(j) / (size - 1)); // u
            vertices.push_back(static_cast<float>(i) / (size - 1)); // v

            if (i < size - 1 && j < size - 1) {
                indices.push_back(static_cast<uint32_t>(j + i * size));
                indices.push_back(static_cast<uint32_t>(j + i * size + size));
                indices.push_back(static_cast<uint32_t>(j + i * size + 1));
                indices.push_back(static_cast<uint32_t>(j + i * size + 1));
                indices.push_back(static_cast<uint32_t>(j + i * size + size));
                indices.push_back(static_cast<uint32_t>(j + i * size + size + 1));
            }
        }
    }
}

/* JONSWAP directional wave spectrum. Returns spectral energy density at
   wave vector (pos_x, pos_y) for a given fetch length and wind velocity. */
double jonswap(double pos_x, double pos_y,
               double fetch, double wind_x, double wind_y,
               double enhancement = 3.3)
{
    using std::pow, std::exp, std::sqrt;

    double freq = sqrt(pos_x * pos_x + pos_y * pos_y);
    if (freq < 0.001) return 0.0;

    double wind  = sqrt(wind_x * wind_x + wind_y * wind_y);
    double g     = 9.81;
    double freq_p = 22.0 * pow(pow(g, 2.0) / (fetch * wind), 1.0 / 3.0);
    double alpha  = 0.076 * pow(pow(wind, 2.0) / (fetch * g), 0.22);
    double sigma  = (freq <= freq_p) ? 0.07 : 0.09;
    double r      = exp(-pow(freq - freq_p, 2.0) / (2.0 * pow(sigma * freq_p, 2.0)));

    double cos_theta = (pos_x * wind_x + pos_y * wind_y) / (freq * wind);
    double dir = std::max(0.0, cos_theta);

    return alpha * (pow(g, 2.0) / pow(freq, 5.0))
         * exp(-5.0 * pow(freq_p / freq, 4.0) / 4.0)
         * pow(enhancement, r)
         * dir * dir;
}

/* Generates the h0(k) initial spectrum and the k-data (wave-vector + dispersion) textures. */
void fill_textures(std::vector<float>& spectrum, std::vector<float>& k_data)
{
    const int N = TEXTURE_SIZE;
    spectrum.assign(N * N * 4, 0.f);
    k_data.assign(N * N * 4, 0.f);

    std::mt19937 gen{ std::random_device{}() };
    std::normal_distribution<double> dist{ 0.0, 1.0 };

    for (int ky = 0; ky < N; ky++) {
        for (int kx = 0; kx < N; kx++) {
            int i  = kx + ky * N;
            int sx = (N - kx) % N;
            int sy = (N - ky) % N;
            int j  = sx + sy * N;

            int kx_m = (kx <= N / 2) ? kx : kx - N;
            int ky_m = (ky <= N / 2) ? ky : ky - N;

            float kx_phys = 2.f * static_cast<float>(std::numbers::pi) * kx_m / PATCH_SIZE;
            float ky_phys = 2.f * static_cast<float>(std::numbers::pi) * ky_m / PATCH_SIZE;
            float k_len   = std::sqrt(kx_phys * kx_phys + ky_phys * ky_phys);
            float omega   = std::sqrt(9.81f * k_len);

            k_data[4 * i + 0] = kx_phys;
            k_data[4 * i + 1] = ky_phys;
            k_data[4 * i + 2] = omega;
            k_data[4 * i + 3] = (k_len > 0.001) ? 1 / k_len : 0;
            
            k_data[4 * j + 0] = -kx_phys;
            k_data[4 * j + 1] = -ky_phys;
            k_data[4 * j + 2] = omega;
            k_data[4 * j + 3] = (k_len > 0.001) ? 1 / k_len : 0;

            double scale = std::sqrt(jonswap(kx_phys, ky_phys, 250000.0, 40.0, 0.0) * 0.5)
                         * WAVE_AMPLITUDE;
            float  re    = static_cast<float>(dist(gen) * scale);
            float  im    = static_cast<float>(dist(gen) * scale);

            if (i == j) {
                spectrum[4 * i]     = re;
                spectrum[4 * i + 1] = 0.f; // force real for DC/Nyquist
            } else {
                spectrum[4 * i]     = re;
                spectrum[4 * i + 1] = im;
                spectrum[4 * j]     = re;
                spectrum[4 * j + 1] = -im; // Hermitian symmetry
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Application lifecycle
// ---------------------------------------------------------------------------

Application::Application(int w, int h) : width(w), height(h)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(width, height, "FFT Ocean", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetCursorPosCallback(window, on_cursor_pos);
    glfwSetScrollCallback(window, on_scroll);

    Instance instance = wgpuCreateInstance(nullptr);
    
	surface = glfwGetWGPUSurface(instance, window);
    RequestAdapterOptions adapter_opts = {};
    adapter_opts.compatibleSurface = surface;
    Adapter adapter = instance.requestAdapter(adapter_opts);
    instance.release();

    DeviceDescriptor device_desc = {};
    device_desc.label = "Main device";
    WGPUFeatureName features[] = { WGPUFeatureName_Float32Filterable };
    device_desc.requiredFeatureCount = 1;
    device_desc.requiredFeatures     = features;
    device_desc.defaultQueue.label   = "Main queue";
    device_desc.deviceLostCallback   = [](WGPUDeviceLostReason reason, char const* msg, void*) {
        std::cout << "Device lost: " << reason;
        if (msg) std::cout << " (" << msg << ")";
        std::cout << '\n';
    };
    RequiredLimits required_limits = get_required_limits(adapter);
    device_desc.requiredLimits = &required_limits;
    device = adapter.requestDevice(device_desc);

    error_callback = device.setUncapturedErrorCallback([](ErrorType type, char const* msg) {
        std::cout << "Uncaptured device error: " << type;
        if (msg) std::cout << " (" << msg << ")";
        std::cout << '\n';
    });

    queue = device.getQueue();

    SurfaceConfiguration surface_config = {};
    surface_config.width       = width;
    surface_config.height      = height;
    surface_config.usage       = TextureUsage::RenderAttachment;
    surface_format             = surface.getPreferredFormat(adapter);
    surface_config.format      = surface_format;
    surface_config.device      = device;
    surface_config.presentMode = PresentMode::Fifo;
    surface_config.alphaMode   = CompositeAlphaMode::Auto;
    surface.configure(surface_config);

    adapter.release();

    init_pipeline();
    init_compute();
    init_buffers();
    init_textures();
    init_bind_groups();
    init_cubemap();
}

Application::~Application()
{
    for (int i = 0; i < 2; i++) {
        height_texture_views[i].release();
        height_textures[i].destroy();
        height_textures[i].release();
        slope_x_texture_views[i].release();
        slope_x_textures[i].destroy();
        slope_x_textures[i].release();
        slope_y_texture_views[i].release();
        slope_y_textures[i].destroy();
        slope_y_textures[i].release();
        disp_x_texture_views[i].release();
        disp_x_textures[i].destroy();
        disp_x_textures[i].release();
        disp_y_texture_views[i].release();
        disp_y_textures[i].destroy();
        disp_y_textures[i].release();
        fold_texture_views[i].release();
        fold_textures[i].destroy();
        fold_textures[i].release();
        foam_texture_views[i].release();
        foam_textures[i].destroy();
        foam_textures[i].release();
        h_fft_bind_groups[i].release();
        sx_fft_bind_groups[i].release();
        sy_fft_bind_groups[i].release();
        dx_fft_bind_groups[i].release();
        dy_fft_bind_groups[i].release();
        fold_fft_bind_groups[i].release();
        foam_bind_groups[i].release();
    }

    time_spectrum_bind_group.release();

    spectrum_texture_view.release();
    spectrum_texture.destroy();
    spectrum_texture.release();

    butterfly_texture_view.release();
    butterfly_texture.destroy();
    butterfly_texture.release();

    k_data_texture_view.release();
    k_data_texture.destroy();
    k_data_texture.release();

    if (cubemap_texture_view) cubemap_texture_view.release();
    if (cubemap_texture)      { cubemap_texture.destroy(); cubemap_texture.release(); }

    bind_group.release();
    pipeline_layout.release();
    bind_group_layout.release();
    time_spectrum_bgl.release();
    time_spectrum_layout.release();
    fft_bgl.release();
    fft_layout.release();
    foam_bgl.release();
    foam_layout.release();

    uniform_buffer.release();
    compute_uniform_buffer.release();
    foam_uniform_buffer.release();
    vertex_buffer.release();
    index_buffer.release();

    depth_texture_view.release();
    depth_texture.destroy();
    depth_texture.release();

    pipeline.release();
    skybox_pipeline.release();
    time_spectrum_pipeline.release();
    fft_h_pipeline.release();
    fft_v_pipeline.release();
    foam_pipeline.release();

    sampler.release();

    surface.unconfigure();
    queue.release();
    surface.release();
    device.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void Application::main_loop()
{
    glfwPollEvents();

    run_compute();

    uniforms.eye_pos    = camera.eye();
    uniforms.view       = camera.view();
    uniforms.projection = glm::perspective(
        glm::radians(45.f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f, PATCH_SIZE * 10.0f);
    uniforms.model      = glm::scale(glm::mat4(1.f), glm::vec3(PATCH_SIZE));
    uniforms.N          = static_cast<float>(TEXTURE_SIZE);
    uniforms.patch_size = PATCH_SIZE;
    uniforms.lambda     = 20.f;//1.2f;
    queue.writeBuffer(uniform_buffer, 0, &uniforms, sizeof(MyUniforms));

    TextureView target = get_next_surface_view(); /// ERROR HERE SOMEWHERE DEBUG LATER
    if (!target) return;

    CommandEncoderDescriptor enc_desc = {};
    enc_desc.label = "Frame encoder";
    CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    RenderPassColorAttachment color_att = {};
    color_att.view       = target;
    color_att.loadOp     = LoadOp::Clear;
    color_att.storeOp    = StoreOp::Store;
    color_att.clearValue = WGPUColor{ 0.05, 0.05, 0.05, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
    color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    RenderPassDepthStencilAttachment depth_att;
    depth_att.view              = depth_texture_view;
    depth_att.depthClearValue   = 1.f;
    depth_att.depthLoadOp       = LoadOp::Clear;
    depth_att.depthStoreOp      = StoreOp::Store;
    depth_att.depthReadOnly     = false;
    depth_att.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
    depth_att.stencilLoadOp  = LoadOp::Clear;
    depth_att.stencilStoreOp = StoreOp::Store;
#else
    depth_att.stencilLoadOp  = LoadOp::Undefined;
    depth_att.stencilStoreOp = StoreOp::Undefined;
#endif
    depth_att.stencilReadOnly = true;

    RenderPassDescriptor pass_desc = {};
    pass_desc.colorAttachmentCount   = 1;
    pass_desc.colorAttachments       = &color_att;
    pass_desc.depthStencilAttachment = &depth_att;

    RenderPassEncoder pass = encoder.beginRenderPass(pass_desc);

    // Water mesh
    pass.setPipeline(pipeline);
    pass.setVertexBuffer(0, vertex_buffer, 0, vertex_buffer.getSize());
    pass.setIndexBuffer(index_buffer, IndexFormat::Uint32, 0, index_buffer.getSize());
    pass.setBindGroup(0, bind_group, 0, nullptr);
    pass.drawIndexed(index_count, 1, 0, 0, 0);

    // Skybox — drawn last, depth LessEqual, no depth write; fills background only
    pass.setPipeline(skybox_pipeline);
    pass.setBindGroup(0, bind_group, 0, nullptr);
    pass.draw(3, 1, 0, 0);

    pass.end();
    pass.release();

    CommandBufferDescriptor cmd_desc = {};
    CommandBuffer command = encoder.finish(cmd_desc);
    encoder.release();
    queue.submit(1, &command);
    command.release();

    target.release();
#ifndef __EMSCRIPTEN__
    surface.present();
#endif
#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#endif
}

bool Application::is_running()
{
    return !glfwWindowShouldClose(window);
}

// ---------------------------------------------------------------------------
// GLFW input callbacks
// ---------------------------------------------------------------------------

void Application::on_mouse_button(GLFWwindow* w, int button, int action, int /*mods*/)
{
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->mouse_dragging = (action == GLFW_PRESS);
        if (app->mouse_dragging)
            glfwGetCursorPos(w, &app->last_mouse_x, &app->last_mouse_y);
    }
}

void Application::on_cursor_pos(GLFWwindow* w, double x, double y)
{
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app->mouse_dragging) return;
    float dx = static_cast<float>(x - app->last_mouse_x);
    float dy = static_cast<float>(y - app->last_mouse_y);
    app->last_mouse_x = x;
    app->last_mouse_y = y;
    app->camera.orbit(dx * 0.005f, -dy * 0.005f);
}

void Application::on_scroll(GLFWwindow* w, double /*dx*/, double dy)
{
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    app->camera.zoom(static_cast<float>(dy));
}

// ---------------------------------------------------------------------------
// Surface texture helper
// ---------------------------------------------------------------------------

TextureView Application::get_next_surface_view()
{
    SurfaceTexture surface_tex;
    surface.getCurrentTexture(&surface_tex);
    if (surface_tex.status != SurfaceGetCurrentTextureStatus::Success) return nullptr;

    Texture texture = surface_tex.texture;

    TextureViewDescriptor view_desc;
    view_desc.label           = "Surface view";
    view_desc.format          = texture.getFormat();
    view_desc.dimension       = TextureViewDimension::_2D;
    view_desc.baseMipLevel    = 0;
    view_desc.mipLevelCount   = 1;
    view_desc.baseArrayLayer  = 0;
    view_desc.arrayLayerCount = 1;
    view_desc.aspect          = TextureAspect::All;

	auto ret = texture.createView(view_desc); 

#ifndef WEBGPU_BACKEND_WGPU
    wgpuTextureRelease(surface_tex.texture);
#endif
    return ret;
}

// ---------------------------------------------------------------------------
// Pipeline initialisation
// ---------------------------------------------------------------------------

void Application::init_pipeline()
{
    ShaderModule water_module  = ResourceManager::load_shader_module(RESOURCE_DIR "/water.wgsl",  device);
    ShaderModule skybox_module = ResourceManager::load_shader_module(RESOURCE_DIR "/skybox.wgsl", device);
    if (!water_module || !skybox_module) {
        std::cerr << "Could not load shaders!\n";
        std::exit(1);
    }

    // --- bind group layout (shared by both render pipelines) ---
    std::vector<BindGroupLayoutEntry> entries = {
        uniform_layout (0, ShaderStage::Vertex | ShaderStage::Fragment, false, sizeof(MyUniforms)),
        texture_layout (1, ShaderStage::Vertex | ShaderStage::Fragment),
        sampler_layout (2, ShaderStage::Fragment),
        texture_layout (3, ShaderStage::Fragment, TextureSampleType::Float, TextureViewDimension::Cube),
        texture_layout (4, ShaderStage::Fragment, TextureSampleType::Float),
        texture_layout (5, ShaderStage::Fragment, TextureSampleType::Float),
        texture_layout (6, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (7, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (8, ShaderStage::Fragment, TextureSampleType::Float),
    };

    BindGroupLayoutDescriptor bgl_desc = {};
    bgl_desc.entryCount = static_cast<uint32_t>(entries.size());
    bgl_desc.entries    = entries.data();
    bind_group_layout   = device.createBindGroupLayout(bgl_desc);

    PipelineLayoutDescriptor layout_desc = {};
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&bind_group_layout);
    pipeline_layout                  = device.createPipelineLayout(layout_desc);

    // --- depth buffer ---
    TextureFormat depth_format = TextureFormat::Depth24Plus;

    TextureDescriptor depth_tex_desc;
    depth_tex_desc.dimension      = TextureDimension::_2D;
    depth_tex_desc.format         = depth_format;
    depth_tex_desc.mipLevelCount  = 1;
    depth_tex_desc.sampleCount    = 1;
    depth_tex_desc.size           = { width, height, 1 };
    depth_tex_desc.usage          = TextureUsage::RenderAttachment;
    depth_tex_desc.viewFormatCount = 1;
    depth_tex_desc.viewFormats    = reinterpret_cast<WGPUTextureFormat*>(&depth_format);
    depth_texture                 = device.createTexture(depth_tex_desc);

    TextureViewDescriptor depth_view_desc;
    depth_view_desc.aspect          = TextureAspect::DepthOnly;
    depth_view_desc.baseArrayLayer  = 0;
    depth_view_desc.arrayLayerCount = 1;
    depth_view_desc.baseMipLevel    = 0;
    depth_view_desc.mipLevelCount   = 1;
    depth_view_desc.dimension       = TextureViewDimension::_2D;
    depth_view_desc.format          = depth_format;
    depth_texture_view              = depth_texture.createView(depth_view_desc);

    // --- shared fragment / color target state ---
    BlendState blend;
    blend.color = { BlendOperation::Add, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha };
    blend.alpha = { BlendOperation::Add, BlendFactor::Zero,     BlendFactor::One };

    ColorTargetState color_target;
    color_target.format    = surface_format;
    color_target.blend     = &blend;
    color_target.writeMask = ColorWriteMask::All;

    DepthStencilState depth_state = Default;
    depth_state.format            = depth_format;
    depth_state.depthCompare      = CompareFunction::Less;
    depth_state.depthWriteEnabled = true;
    depth_state.stencilReadMask   = 0;
    depth_state.stencilWriteMask  = 0;

    // --- water render pipeline ---
    std::vector<VertexAttribute> attribs(2);
    attribs[0].format         = VertexFormat::Float32x3;
    attribs[0].offset         = 0;
    attribs[0].shaderLocation = 0;
    attribs[1].format         = VertexFormat::Float32x2;
    attribs[1].offset         = 3 * sizeof(float);
    attribs[1].shaderLocation = 1;

    VertexBufferLayout vbl;
    vbl.attributeCount = static_cast<uint32_t>(attribs.size());
    vbl.attributes     = attribs.data();
    vbl.arrayStride    = 5 * sizeof(float);
    vbl.stepMode       = VertexStepMode::Vertex;

    FragmentState water_fragment;
    water_fragment.module        = water_module;
    water_fragment.entryPoint    = "fs_main";
    water_fragment.constantCount = 0;
    water_fragment.constants     = nullptr;
    water_fragment.targetCount   = 1;
    water_fragment.targets       = &color_target;

    RenderPipelineDescriptor water_desc;
    water_desc.vertex.module       = water_module;
    water_desc.vertex.entryPoint   = "vs_main";
    water_desc.vertex.bufferCount  = 1;
    water_desc.vertex.buffers      = &vbl;
    water_desc.vertex.constantCount = 0;
    water_desc.vertex.constants    = nullptr;
    water_desc.primitive.topology         = PrimitiveTopology::TriangleList;
    water_desc.primitive.stripIndexFormat = IndexFormat::Undefined;
    water_desc.primitive.frontFace        = FrontFace::CCW;
    water_desc.primitive.cullMode         = CullMode::None;
    water_desc.fragment                   = &water_fragment;
    water_desc.depthStencil               = &depth_state;
    water_desc.multisample.count          = 1;
    water_desc.multisample.mask           = ~0u;
    water_desc.multisample.alphaToCoverageEnabled = false;
    water_desc.layout                     = pipeline_layout;
    pipeline = device.createRenderPipeline(water_desc);

    // --- skybox render pipeline (no vertex buffer, depth LessEqual, no depth write) ---
    DepthStencilState skybox_depth = Default;
    skybox_depth.format            = depth_format;
    skybox_depth.depthCompare      = CompareFunction::LessEqual;
    skybox_depth.depthWriteEnabled = false;
    skybox_depth.stencilReadMask   = 0;
    skybox_depth.stencilWriteMask  = 0;

    FragmentState skybox_fragment;
    skybox_fragment.module        = skybox_module;
    skybox_fragment.entryPoint    = "fs_skybox";
    skybox_fragment.constantCount = 0;
    skybox_fragment.constants     = nullptr;
    skybox_fragment.targetCount   = 1;
    skybox_fragment.targets       = &color_target;

    RenderPipelineDescriptor skybox_desc;
    skybox_desc.vertex.module        = skybox_module;
    skybox_desc.vertex.entryPoint    = "vs_skybox";
    skybox_desc.vertex.bufferCount   = 0;
    skybox_desc.vertex.buffers       = nullptr;
    skybox_desc.vertex.constantCount = 0;
    skybox_desc.vertex.constants     = nullptr;
    skybox_desc.primitive.topology         = PrimitiveTopology::TriangleList;
    skybox_desc.primitive.stripIndexFormat = IndexFormat::Undefined;
    skybox_desc.primitive.frontFace        = FrontFace::CCW;
    skybox_desc.primitive.cullMode         = CullMode::None;
    skybox_desc.fragment                   = &skybox_fragment;
    skybox_desc.depthStencil               = &skybox_depth;
    skybox_desc.multisample.count          = 1;
    skybox_desc.multisample.mask           = ~0u;
    skybox_desc.multisample.alphaToCoverageEnabled = false;
    skybox_desc.layout                     = pipeline_layout;
    skybox_pipeline = device.createRenderPipeline(skybox_desc);

    water_module.release();
    skybox_module.release();
}

// ---------------------------------------------------------------------------
// Compute pipeline initialisation
// ---------------------------------------------------------------------------

void Application::init_compute()
{
    // --- time_spectrum pipeline (time_spectrum.wgsl) ---
    {
        ShaderModule ts_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/time_spectrum.wgsl", device);

        std::vector<BindGroupLayoutEntry> ts_entries = {
            uniform_layout        (0, ShaderStage::Compute, false, sizeof(ComputeUniforms)),
            storage_texture_layout(1, ShaderStage::Compute),
            texture_layout        (2, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            texture_layout        (3, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            storage_texture_layout(4, ShaderStage::Compute),
            storage_texture_layout(5, ShaderStage::Compute),
            storage_texture_layout(6, ShaderStage::Compute),
            storage_texture_layout(7, ShaderStage::Compute),
            storage_texture_layout(8, ShaderStage::Compute),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(ts_entries.size());
        bgl_desc.entries    = ts_entries.data();
        time_spectrum_bgl   = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&time_spectrum_bgl);
        time_spectrum_layout             = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = time_spectrum_layout;
        pipe_desc.compute.module        = ts_module;
        pipe_desc.compute.entryPoint    = "timeSpectrum";
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;
        time_spectrum_pipeline = device.createComputePipeline(pipe_desc);

        ts_module.release();
    }

    // --- FFT pipeline (fft.wgsl) ---
    {
        ShaderModule fft_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/fft.wgsl", device);

        std::vector<BindGroupLayoutEntry> fft_entries = {
            uniform_layout        (0, ShaderStage::Compute, true, sizeof(ComputeUniforms)),
            storage_texture_layout(1, ShaderStage::Compute),
            texture_layout        (2, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            texture_layout        (3, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(fft_entries.size());
        bgl_desc.entries    = fft_entries.data();
        fft_bgl             = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&fft_bgl);
        fft_layout                       = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = fft_layout;
        pipe_desc.compute.module        = fft_module;
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;

        pipe_desc.compute.entryPoint = "fft_horizontal";
        fft_h_pipeline = device.createComputePipeline(pipe_desc);

        pipe_desc.compute.entryPoint = "fft_vertical";
        fft_v_pipeline = device.createComputePipeline(pipe_desc);

        fft_module.release();
    }

    // --- foam pipeline (foam.wgsl) ---
    {
        ShaderModule foam_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/foam.wgsl", device);

        std::vector<BindGroupLayoutEntry> foam_entries = {
            uniform_layout        (0, ShaderStage::Compute, false, sizeof(FoamUniforms)),
            texture_layout        (1, ShaderStage::Compute, TextureSampleType::Float),
            texture_layout        (2, ShaderStage::Compute, TextureSampleType::Float),
            storage_texture_layout(3, ShaderStage::Compute, TextureFormat::R32Float),
            sampler_layout        (4, ShaderStage::Compute),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(foam_entries.size());
        bgl_desc.entries    = foam_entries.data();
        foam_bgl            = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&foam_bgl);
        foam_layout                      = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = foam_layout;
        pipe_desc.compute.module        = foam_module;
        pipe_desc.compute.entryPoint    = "computeFoam";
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;
        foam_pipeline = device.createComputePipeline(pipe_desc);

        foam_module.release();
    }
}

// ---------------------------------------------------------------------------
// Compute dispatch
// ---------------------------------------------------------------------------

void Application::run_compute()
{
    /* Pre-fill all TEXTURE_LOG uniform slots before encoding so each FFT stage
       dispatch can select its slot via a dynamic offset within a single pass. */
    const float t = static_cast<float>(glfwGetTime());
    std::vector<uint8_t> ubuf(compute_uniform_stride * TEXTURE_LOG, 0);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        ComputeUniforms cu{ t, static_cast<uint32_t>(s), TEXTURE_SIZE, TEXTURE_LOG };
        std::memcpy(ubuf.data() + s * compute_uniform_stride, &cu, sizeof(ComputeUniforms));
    }
    queue.writeBuffer(compute_uniform_buffer, 0, ubuf.data(), ubuf.size());

    FoamUniforms fu{ uniforms.lambda, 0.9f, 0.003f,
                     static_cast<float>(TEXTURE_SIZE), static_cast<float>(FOAM_SIZE) };
    queue.writeBuffer(foam_uniform_buffer, 0, &fu, sizeof(FoamUniforms));

    CommandEncoder encoder = device.createCommandEncoder(CommandEncoderDescriptor{});

    ComputePassDescriptor pass_desc;
    pass_desc.timestampWrites = nullptr;
    ComputePassEncoder pass   = encoder.beginComputePass(pass_desc);

    /* timeSpectrum: evolve h0(k) → h(k,t) and compute slope spectra sx(k), sy(k).
       Outputs land in height[0], slope_x[0], slope_y[0] (slot 0 = stage 0, time t). */
    pass.setPipeline(time_spectrum_pipeline);
    pass.setBindGroup(0, time_spectrum_bind_group, 0, nullptr);
    pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 32, 1);

    /* Horizontal IFFT for all three textures, 8 stages each.
       Ping-pong index: stage s uses bind_groups[1 - s%2] so output lands in [0] after
       8 stages (last odd stage writes to [0]). Height and slope share the same pattern. */
    pass.setPipeline(fft_h_pipeline);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        uint32_t off = static_cast<uint32_t>(s) * compute_uniform_stride;
        pass.setBindGroup(0, h_fft_bind_groups   [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
        pass.setBindGroup(0, sx_fft_bind_groups  [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
        pass.setBindGroup(0, sy_fft_bind_groups  [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
        pass.setBindGroup(0, dx_fft_bind_groups  [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
        pass.setBindGroup(0, dy_fft_bind_groups  [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
        pass.setBindGroup(0, fold_fft_bind_groups[1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
    }

    /* Vertical IFFT — transposed dispatch. After TEXTURE_LOG horizontal stages, the result
       sits in tex[TEXTURE_LOG % 2], so the vertical pass must offset its ping-pong index
       by TEXTURE_LOG to start reading from the correct texture. */
    pass.setPipeline(fft_v_pipeline);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        uint32_t off = static_cast<uint32_t>(s) * compute_uniform_stride;
        int bg = (TEXTURE_LOG + s + 1) % 2;
        pass.setBindGroup(0, h_fft_bind_groups   [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
        pass.setBindGroup(0, sx_fft_bind_groups  [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
        pass.setBindGroup(0, sy_fft_bind_groups  [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
        pass.setBindGroup(0, dx_fft_bind_groups  [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
        pass.setBindGroup(0, dy_fft_bind_groups  [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
        pass.setBindGroup(0, fold_fft_bind_groups[bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
    }

    /* Foam accumulation: mark wave-breaking pixels (J < threshold), erode previous foam. */
    pass.setPipeline(foam_pipeline);
    pass.setBindGroup(0, foam_bind_groups[foam_frame % 2], 0, nullptr);
    pass.dispatchWorkgroups(FOAM_SIZE / 8, FOAM_SIZE / 8, 1);

    pass.end();
#ifndef WEBGPU_BACKEND_WGPU
    wgpuComputePassEncoderRelease(pass);
#endif

    CommandBuffer commands = encoder.finish(CommandBufferDescriptor{});
    queue.submit(commands);
#ifndef WEBGPU_BACKEND_WGPU
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
#endif

    /* Point the render bind group at the foam texture just written, then advance counter. */
    rebuild_render_bind_group(1 - static_cast<int>(foam_frame % 2));
    foam_frame++;
}

// ---------------------------------------------------------------------------
// Device limits
// ---------------------------------------------------------------------------

RequiredLimits Application::get_required_limits(Adapter adapter) const
{
    SupportedLimits supported;
    adapter.getLimits(&supported);

    RequiredLimits limits = Default;
    limits.limits.maxVertexAttributes       = 2;
    limits.limits.maxVertexBuffers          = 1;
    limits.limits.maxBufferSize             = static_cast<uint64_t>(MESH_SIZE) * MESH_SIZE * 6 * sizeof(uint32_t);
    limits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);
    limits.limits.maxBindGroups             = 1;
    limits.limits.maxUniformBuffersPerShaderStage = 1;
    limits.limits.maxUniformBufferBindingSize     = sizeof(MyUniforms);
    limits.limits.maxTextureDimension1D     = std::max({ width, height, TEXTURE_SIZE, FOAM_SIZE });
    limits.limits.maxTextureDimension2D     = std::max({ width, height, TEXTURE_SIZE, FOAM_SIZE });
    limits.limits.maxTextureArrayLayers     = 6;
    limits.limits.maxSampledTexturesPerShaderStage  = 5;
    limits.limits.maxStorageTexturesPerShaderStage  = 6;
    limits.limits.maxSamplersPerShaderStage         = 1;
    limits.limits.minUniformBufferOffsetAlignment  = supported.limits.minUniformBufferOffsetAlignment;
    limits.limits.minStorageBufferOffsetAlignment  = supported.limits.minStorageBufferOffsetAlignment;
    limits.limits.maxComputeWorkgroupSizeX         = 1024;
    limits.limits.maxComputeWorkgroupSizeY         = 32;
    limits.limits.maxComputeWorkgroupSizeZ         = 1;
    limits.limits.maxComputeInvocationsPerWorkgroup = 1024;
    return limits;
}

// ---------------------------------------------------------------------------
// Buffer initialisation
// ---------------------------------------------------------------------------

void Application::init_buffers()
{
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    create_geometry(MESH_SIZE, vertices, indices);

    index_count = static_cast<uint32_t>(indices.size());

    BufferDescriptor buf_desc;
    buf_desc.mappedAtCreation = false;

    buf_desc.size  = vertices.size() * sizeof(float);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    vertex_buffer  = device.createBuffer(buf_desc);
    queue.writeBuffer(vertex_buffer, 0, vertices.data(), buf_desc.size);

    buf_desc.size  = indices.size() * sizeof(uint32_t);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Index;
    index_buffer   = device.createBuffer(buf_desc);
    queue.writeBuffer(index_buffer, 0, indices.data(), buf_desc.size);

    buf_desc.size  = sizeof(MyUniforms);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    uniform_buffer = device.createBuffer(buf_desc);

    /* 256-byte aligned slots for dynamic-offset compute uniforms (one per FFT stage). */
    compute_uniform_stride = (sizeof(ComputeUniforms) + 255u) & ~255u;
    buf_desc.size          = compute_uniform_stride * TEXTURE_LOG;
    buf_desc.usage         = BufferUsage::CopyDst | BufferUsage::Uniform;
    compute_uniform_buffer = device.createBuffer(buf_desc);

    buf_desc.size          = sizeof(FoamUniforms);
    buf_desc.usage         = BufferUsage::CopyDst | BufferUsage::Uniform;
    foam_uniform_buffer    = device.createBuffer(buf_desc);
}

// ---------------------------------------------------------------------------
// Bind group management
// ---------------------------------------------------------------------------

void Application::rebuild_render_bind_group(int foam_idx)
{
    if (bind_group) bind_group.release();

    std::vector<BindGroupEntry> entries(9, Default);
    entries[0].binding = 0;
    entries[0].buffer  = uniform_buffer;
    entries[0].offset  = 0;
    entries[0].size    = sizeof(MyUniforms);

    entries[1].binding     = 1;
    entries[1].textureView = height_texture_views[0];

    entries[2].binding = 2;
    entries[2].sampler = sampler;

    entries[3].binding     = 3;
    entries[3].textureView = cubemap_texture_view;

    entries[4].binding     = 4;
    entries[4].textureView = slope_x_texture_views[0];

    entries[5].binding     = 5;
    entries[5].textureView = slope_y_texture_views[0];

    entries[6].binding     = 6;
    entries[6].textureView = disp_x_texture_views[0];

    entries[7].binding     = 7;
    entries[7].textureView = disp_y_texture_views[0];

    entries[8].binding     = 8;
    entries[8].textureView = foam_texture_views[foam_idx];

    BindGroupDescriptor desc;
    desc.layout     = bind_group_layout;
    desc.entryCount = static_cast<uint32_t>(entries.size());
    desc.entries    = entries.data();
    bind_group      = device.createBindGroup(desc);
}

void Application::init_bind_groups()
{
    // --- time_spectrum bind group ---
    {
        std::vector<BindGroupEntry> e(9, Default);
        e[0].binding = 0;  e[0].buffer      = compute_uniform_buffer;
                           e[0].offset       = 0;
                           e[0].size         = sizeof(ComputeUniforms);
        e[1].binding = 1;  e[1].textureView  = height_texture_views[0];
        e[2].binding = 2;  e[2].textureView  = spectrum_texture_view;
        e[3].binding = 3;  e[3].textureView  = k_data_texture_view;
        e[4].binding = 4;  e[4].textureView  = slope_x_texture_views[0];
        e[5].binding = 5;  e[5].textureView  = slope_y_texture_views[0];
        e[6].binding = 6;  e[6].textureView  = disp_x_texture_views[0];
        e[7].binding = 7;  e[7].textureView  = disp_y_texture_views[0];
        e[8].binding = 8;  e[8].textureView  = fold_texture_views[0];

        BindGroupDescriptor desc;
        desc.layout     = time_spectrum_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();
        time_spectrum_bind_group = device.createBindGroup(desc);
    }

    // --- FFT bind groups (all 6 IFFT channels — ping-pong pairs) ---
    {
        /* 4-entry template: uniform(0), out(1), in(2), butterfly(3) */
        std::vector<BindGroupEntry> e(4, Default);
        e[0].binding = 0;  e[0].buffer     = compute_uniform_buffer;
                           e[0].offset      = 0;
                           e[0].size        = sizeof(ComputeUniforms);
        e[3].binding = 3;  e[3].textureView = butterfly_texture_view;

        BindGroupDescriptor desc;
        desc.layout     = fft_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();

        auto make_pair = [&](wgpu::TextureView* views, wgpu::BindGroup* groups) {
            e[1].binding = 1;  e[1].textureView = views[0];
            e[2].binding = 2;  e[2].textureView = views[1];
            groups[0] = device.createBindGroup(desc);   /* out=[0], in=[1] */

            e[1].textureView = views[1];
            e[2].textureView = views[0];
            groups[1] = device.createBindGroup(desc);   /* out=[1], in=[0] */
        };

        make_pair(height_texture_views,  h_fft_bind_groups);
        make_pair(slope_x_texture_views, sx_fft_bind_groups);
        make_pair(slope_y_texture_views, sy_fft_bind_groups);
        make_pair(disp_x_texture_views,  dx_fft_bind_groups);
        make_pair(disp_y_texture_views,  dy_fft_bind_groups);
        make_pair(fold_texture_views,    fold_fft_bind_groups);
    }

    // --- foam bind groups ([i]: reads foam[i], writes foam[1-i]) ---
    {
        std::vector<BindGroupEntry> e(5, Default);
        e[0].binding = 0;  e[0].buffer = foam_uniform_buffer;
                           e[0].offset  = 0;
                           e[0].size    = sizeof(FoamUniforms);
        e[1].binding = 1;  e[1].textureView = fold_texture_views[0];
        e[4].binding = 4;  e[4].sampler     = sampler;

        BindGroupDescriptor desc;
        desc.layout     = foam_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();

        for (int i = 0; i < 2; i++) {
            e[2].binding = 2;  e[2].textureView = foam_texture_views[i];       /* prev */
            e[3].binding = 3;  e[3].textureView = foam_texture_views[1 - i];   /* out  */
            foam_bind_groups[i] = device.createBindGroup(desc);
        }
    }
}

// ---------------------------------------------------------------------------
// Texture initialisation
// ---------------------------------------------------------------------------

void Application::init_textures()
{
    using wgpu::TextureUsage, wgpu::TextureFormat;

    const WGPUTextureUsageFlags ping_pong_usage = TextureUsage::TextureBinding | TextureUsage::StorageBinding;
    const WGPUTextureUsageFlags upload_usage    = TextureUsage::TextureBinding | TextureUsage::CopyDst;

    /* Ping-pong height-field textures for the IFFT pipeline. */
    for (int i = 0; i < 2; i++) {
        height_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                    TextureFormat::RGBA32Float, ping_pong_usage);
        height_texture_views[i] = create_view_2d(height_textures[i], TextureFormat::RGBA32Float);
    }

    /* Ping-pong slope textures for spectral-derivative normals (∂h/∂x and ∂h/∂y). */
    for (int i = 0; i < 2; i++) {
        slope_x_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                      TextureFormat::RGBA32Float, ping_pong_usage);
        slope_x_texture_views[i] = create_view_2d(slope_x_textures[i], TextureFormat::RGBA32Float);
        slope_y_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                      TextureFormat::RGBA32Float, ping_pong_usage);
        slope_y_texture_views[i] = create_view_2d(slope_y_textures[i], TextureFormat::RGBA32Float);
    }

    /* Ping-pong choppy displacement textures (Dx, Dy) and Jacobian fold. */
    for (int i = 0; i < 2; i++) {
        disp_x_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                     TextureFormat::RGBA32Float, ping_pong_usage);
        disp_x_texture_views[i] = create_view_2d(disp_x_textures[i], TextureFormat::RGBA32Float);
        disp_y_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                     TextureFormat::RGBA32Float, ping_pong_usage);
        disp_y_texture_views[i] = create_view_2d(disp_y_textures[i], TextureFormat::RGBA32Float);
        fold_textures[i]        = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                     TextureFormat::RGBA32Float, ping_pong_usage);
        fold_texture_views[i]   = create_view_2d(fold_textures[i], TextureFormat::RGBA32Float);
    }

    /* Foam ping-pong textures (R32Float, writable by compute + readable by render).
       CopyDst is required for the explicit zero-fill below — D3D12 does not guarantee
       zero-initialization for StorageBinding-only textures in practice. */
    const WGPUTextureUsageFlags foam_usage =
        TextureUsage::TextureBinding | TextureUsage::StorageBinding | TextureUsage::CopyDst;
    {
        std::vector<float> zeros(FOAM_SIZE * FOAM_SIZE, 0.0f);
        for (int i = 0; i < 2; i++) {
            foam_textures[i]      = create_texture_2d(device, FOAM_SIZE, FOAM_SIZE,
                                                       TextureFormat::R32Float, foam_usage);
            foam_texture_views[i] = create_view_2d(foam_textures[i], TextureFormat::R32Float);

            ImageCopyTexture dst = {};
            dst.texture  = foam_textures[i];
            dst.mipLevel = 0;
            dst.origin   = { 0, 0, 0 };
            dst.aspect   = TextureAspect::All;
            TextureDataLayout layout = {};
            layout.bytesPerRow  = FOAM_SIZE * sizeof(float);
            layout.rowsPerImage = FOAM_SIZE;
            Extent3D extent = { FOAM_SIZE, FOAM_SIZE, 1 };
            queue.writeTexture(dst, zeros.data(), zeros.size() * sizeof(float), layout, extent);
        }
    }

    /* Initial h0(k) spectrum — generated once on the CPU, uploaded, then read-only. */
    spectrum_texture      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                              TextureFormat::RGBA32Float, upload_usage);
    spectrum_texture_view = create_view_2d(spectrum_texture, TextureFormat::RGBA32Float);

    /* Wave-vector and dispersion data (kx, ky, omega, unused). */
    k_data_texture      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                            TextureFormat::RGBA32Float, upload_usage);
    k_data_texture_view = create_view_2d(k_data_texture, TextureFormat::RGBA32Float);

    /* Generate and upload spectrum + k-data. */
    std::vector<float> spectrum, k_data;
    fill_textures(spectrum, k_data);

    auto upload = [&](Texture tex, const void* data, size_t bytes,
                      uint32_t bytes_per_row, uint32_t rows) {
        ImageCopyTexture dst = {};
        dst.texture  = tex;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, 0 };
        dst.aspect   = TextureAspect::All;
        TextureDataLayout layout = {};
        layout.bytesPerRow  = bytes_per_row;
        layout.rowsPerImage = rows;
        Extent3D extent = { TEXTURE_SIZE, TEXTURE_SIZE, 1 };
        queue.writeTexture(dst, data, bytes, layout, extent);
    };

    upload(spectrum_texture, spectrum.data(), spectrum.size() * sizeof(float),
           TEXTURE_SIZE * 4 * sizeof(float), TEXTURE_SIZE);
    upload(k_data_texture, k_data.data(), k_data.size() * sizeof(float),
           TEXTURE_SIZE * 4 * sizeof(float), TEXTURE_SIZE);

    /* Butterfly twiddle table: (N/2) × log2(N), RGBA32Float.
       Each texel (x, stage) = (tw.re, tw.im, a_idx, b_idx) for DIT IFFT. */
    butterfly_texture      = create_texture_2d(device, TEXTURE_SIZE / 2, TEXTURE_LOG,
                                               TextureFormat::RGBA32Float, upload_usage);
    butterfly_texture_view = create_view_2d(butterfly_texture, TextureFormat::RGBA32Float);

    {
        const float pi = static_cast<float>(std::numbers::pi);
        std::vector<float> bfly(TEXTURE_SIZE / 2 * TEXTURE_LOG * 4);
        for (unsigned s = 0; s < TEXTURE_LOG; s++) {
            int half_span = 1 << s;
            int span      = 2 * half_span;
            for (unsigned x = 0; x < TEXTURE_SIZE / 2; x++) {
                int local_j = x % half_span;
                int group   = x / half_span;
                int a_idx   = group * span + local_j;
                int b_idx   = a_idx + half_span;
                int k       = local_j * (TEXTURE_SIZE / span);
                float angle = +2.f * pi * k / TEXTURE_SIZE; // + sign = IFFT
                int   idx   = (x + s * (TEXTURE_SIZE / 2)) * 4;
                bfly[idx + 0] = std::cos(angle); // tw.re
                bfly[idx + 1] = std::sin(angle); // tw.im
                bfly[idx + 2] = static_cast<float>(a_idx);
                bfly[idx + 3] = static_cast<float>(b_idx);
            }
        }
        ImageCopyTexture dst = {};
        dst.texture  = butterfly_texture;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, 0 };
        dst.aspect   = TextureAspect::All;
        TextureDataLayout layout = {};
        layout.bytesPerRow  = (TEXTURE_SIZE / 2) * 4 * sizeof(float);
        layout.rowsPerImage = TEXTURE_LOG;
        Extent3D extent = { static_cast<uint32_t>(TEXTURE_SIZE / 2), TEXTURE_LOG, 1 };
        queue.writeTexture(dst, bfly.data(), bfly.size() * sizeof(float), layout, extent);
    }

    /* Sampler shared by both the water shader and the skybox. */
    SamplerDescriptor sampler_desc;
    sampler_desc.addressModeU  = AddressMode::Repeat;
    sampler_desc.addressModeV  = AddressMode::Repeat;
    sampler_desc.addressModeW  = AddressMode::Repeat;
    sampler_desc.magFilter     = FilterMode::Linear;
    sampler_desc.minFilter     = FilterMode::Linear;
    sampler_desc.mipmapFilter  = MipmapFilterMode::Linear;
    sampler_desc.lodMinClamp   = 0.f;
    sampler_desc.lodMaxClamp   = 1.f;
    sampler_desc.compare       = CompareFunction::Undefined;
    sampler_desc.maxAnisotropy = 1;
    sampler = device.createSampler(sampler_desc);
}

// ---------------------------------------------------------------------------
// Cubemap loading
// ---------------------------------------------------------------------------

void Application::init_cubemap()
{
    namespace fs = std::filesystem;
    for (auto const& entry : fs::directory_iterator(fs::path(RESOURCE_DIR "/Cubemap"))) {
        if (entry.path().extension() == ".png")
            cubemap_paths.push_back(entry.path().string());
    }
    std::sort(cubemap_paths.begin(), cubemap_paths.end());

    if (!cubemap_paths.empty())
        load_cubemap(0);
}

void Application::load_cubemap(int index)
{
    if (index < 0 || index >= static_cast<int>(cubemap_paths.size())) return;
    cubemap_index = index;

    std::vector<uint8_t> face_pixels;
    int face_size = 0;
    if (!ResourceManager::load_cubemap_cross(cubemap_paths[index], face_pixels, face_size)) {
        std::cerr << "Failed to load cubemap: " << cubemap_paths[index] << '\n';
        return;
    }

    if (cubemap_texture_view) cubemap_texture_view.release();
    if (cubemap_texture)      { cubemap_texture.destroy(); cubemap_texture.release(); }

    TextureDescriptor tex_desc;
    tex_desc.dimension       = TextureDimension::_2D;
    tex_desc.size            = { static_cast<uint32_t>(face_size), static_cast<uint32_t>(face_size), 6 };
    tex_desc.mipLevelCount   = 1;
    tex_desc.sampleCount     = 1;
    tex_desc.format          = TextureFormat::RGBA8Unorm;
    tex_desc.usage           = TextureUsage::TextureBinding | TextureUsage::CopyDst;
    tex_desc.viewFormatCount = 0;
    tex_desc.viewFormats     = nullptr;
    cubemap_texture = device.createTexture(tex_desc);

    TextureViewDescriptor view_desc;
    view_desc.aspect          = TextureAspect::All;
    view_desc.baseArrayLayer  = 0;
    view_desc.arrayLayerCount = 6;
    view_desc.baseMipLevel    = 0;
    view_desc.mipLevelCount   = 1;
    view_desc.dimension       = TextureViewDimension::Cube;
    view_desc.format          = TextureFormat::RGBA8Unorm;
    cubemap_texture_view      = cubemap_texture.createView(view_desc);

    const int bytes_per_face = face_size * face_size * 4;
    for (int face = 0; face < 6; face++) {
        ImageCopyTexture dst = {};
        dst.texture  = cubemap_texture;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, static_cast<uint32_t>(face) };
        dst.aspect   = TextureAspect::All;

        TextureDataLayout layout = {};
        layout.bytesPerRow  = static_cast<uint32_t>(face_size) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(face_size);

        Extent3D extent = { static_cast<uint32_t>(face_size), static_cast<uint32_t>(face_size), 1 };
        queue.writeTexture(dst, face_pixels.data() + face * bytes_per_face,
                           static_cast<size_t>(bytes_per_face), layout, extent);
    }

    rebuild_render_bind_group();
}
