
#define WEBGPU_CPP_IMPLEMENTATION

#include "Application.h"

#include <algorithm>
#include <iostream>

using namespace wgpu;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Application::Application(int w, int h)
    : width(static_cast<uint32_t>(w)), height(static_cast<uint32_t>(h))
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(w, h, "FFT Ocean", nullptr, nullptr);
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

    /* Camera — initialised from CameraConfig. */
    camera.theta             = config.camera.theta;
    camera.phi               = config.camera.phi;
    camera.radius            = config.ocean.patch_size * config.camera.radius_factor;
    camera.orbit_sensitivity = config.camera.orbit_sensitivity;
    camera.zoom_factor       = config.camera.zoom_factor;
    camera.zoom_min          = config.camera.zoom_min;
    camera.zoom_max          = config.camera.zoom_max;

    /* Subsystem initialisation. */
    ocean.init(device, queue, config);
    renderer.init(device, queue, surface_format, width, height, config);
    renderer.init_cubemap(config);
    renderer.rebuild_bind_group(ocean, foam_idx);

    /* ImGui — must come after all WebGPU resources are created. */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplWGPU_Init(
        device, 2, static_cast<WGPUTextureFormat>(surface_format), WGPUTextureFormat_Depth24Plus
    );

    /* Register UI panels. */
    ui_panels.push_back([this]() {
        ImGui::Begin("Ocean");
        ImGui::SliderFloat("Choppiness",    &config.ocean.lambda,      0.f,    40.f);
        ImGui::SliderFloat("Patch size",    &config.ocean.patch_size,  16.f,  512.f);
        ImGui::InputDouble("Wave amplitude", &config.ocean.wave_amplitude);
        ImGui::InputDouble("Wind speed X",   &config.ocean.wind_x);
        ImGui::InputDouble("Wind speed Y",   &config.ocean.wind_y);
        ImGui::InputDouble("Fetch",          &config.ocean.fetch);
        if (ImGui::Button("Rebuild spectrum"))
            ocean.rebuild_spectrum(config);
        ImGui::End();
    });

    ui_panels.push_back([this]() {
        ImGui::Begin("Foam");
        ImGui::SliderFloat("Threshold", &config.foam.threshold, 0.7f, 1.1f);
        ImGui::SliderFloat("Erosion",   &config.foam.erosion,   0.9f, 1.0f);
        ImGui::SliderFloat("Foam add",  &config.foam.foam_add,  0.f, 5.f);
        ImGui::End();
    });

    ui_panels.push_back([]() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(10.f, io.DisplaySize.y - 10.f), ImGuiCond_Always, ImVec2(0.f, 1.f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##controls", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::Text("        CONTROLS");
        ImGui::Text("Move camera - LMB + drag");
        ImGui::Text("   Zoom     -  Scroll");
        ImGui::End();
    });
}

Application::~Application()
{
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    /* OceanSim and Renderer destructors release their own GPU resources. */

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

    foam_idx = ocean.tick(static_cast<float>(glfwGetTime()), config);
    renderer.rebuild_bind_group(ocean, foam_idx);

    uniforms.eye_pos    = camera.eye();
    uniforms.view       = camera.view();
    uniforms.projection = glm::perspective(
        glm::radians(45.f),
        static_cast<float>(width) / static_cast<float>(height),
        0.01f, config.ocean.patch_size * 20.f);
    uniforms.model      = glm::scale(glm::mat4(1.f), glm::vec3(config.ocean.patch_size));
    uniforms.N          = static_cast<float>(TEXTURE_SIZE);
    uniforms.patch_size = config.ocean.patch_size;
    uniforms.lambda     = config.ocean.lambda;

    TextureView target = get_next_surface_view();
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
    depth_att.view              = renderer.depth_view();
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

    renderer.draw(pass, uniforms);

    pass.pushDebugGroup("ImGui");
    render_ui(pass);
    pass.popDebugGroup();

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
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        app->mouse_dragging = (action == GLFW_PRESS);
        if (app->mouse_dragging)
            glfwGetCursorPos(w, &app->last_mouse_x, &app->last_mouse_y);
    }
}

void Application::on_cursor_pos(GLFWwindow* w, double x, double y)
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!app->mouse_dragging) return;
    float dx = static_cast<float>(x - app->last_mouse_x);
    float dy = static_cast<float>(y - app->last_mouse_y);
    app->last_mouse_x = x;
    app->last_mouse_y = y;
    app->camera.orbit(dx * app->camera.orbit_sensitivity, -dy * app->camera.orbit_sensitivity);
}

void Application::on_scroll(GLFWwindow* w, double /*dx*/, double dy)
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    app->camera.zoom(static_cast<float>(dy));
}

// ---------------------------------------------------------------------------
// ImGui rendering
// ---------------------------------------------------------------------------

void Application::render_ui(wgpu::RenderPassEncoder pass)
{
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    for (auto& panel : ui_panels)
        panel();

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
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
// Device limits
// ---------------------------------------------------------------------------

RequiredLimits Application::get_required_limits(Adapter adapter) const
{
    SupportedLimits supported;
    adapter.getLimits(&supported);

    RequiredLimits limits = Default;
    limits.limits.maxVertexAttributes       = 3;
    limits.limits.maxVertexBuffers          = 1;
    limits.limits.maxBufferSize             = static_cast<uint64_t>(MESH_SIZE) * MESH_SIZE * 6 * sizeof(uint32_t);
    limits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);
    limits.limits.maxBindGroups             = 2;
    limits.limits.maxUniformBuffersPerShaderStage = 1;
    limits.limits.maxUniformBufferBindingSize     = sizeof(RenderUniforms);
    limits.limits.maxTextureDimension1D     = std::max({ width, height, TEXTURE_SIZE });
    limits.limits.maxTextureDimension2D     = std::max({ width, height, TEXTURE_SIZE });
    limits.limits.maxTextureArrayLayers     = 6;
    limits.limits.maxSampledTexturesPerShaderStage  = 6;
    limits.limits.maxStorageTexturesPerShaderStage  = 6;
    limits.limits.maxSamplersPerShaderStage         = 1;
    limits.limits.minUniformBufferOffsetAlignment   = supported.limits.minUniformBufferOffsetAlignment;
    limits.limits.minStorageBufferOffsetAlignment   = supported.limits.minStorageBufferOffsetAlignment;
    limits.limits.maxComputeWorkgroupSizeX          = 1024;
    limits.limits.maxComputeWorkgroupSizeY          = 16;
    limits.limits.maxComputeWorkgroupSizeZ          = 1;
    limits.limits.maxComputeInvocationsPerWorkgroup = 1024;
    return limits;
}
