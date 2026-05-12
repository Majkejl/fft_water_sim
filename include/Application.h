#pragma once

#include "webgpu/webgpu.hpp"
#include "webgpu-utils.h"
#include "Camera.h"
#include "SimulationConfig.h"
#include "OceanSim.h"
#include "Renderer.h"
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <functional>
#include <iostream>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif

/* Top-level application class.
   Owns the WebGPU device, drives the main loop, and orchestrates OceanSim + Renderer. */
class Application
{
    // --- config ---
    SimulationConfig config;

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

    // --- subsystems ---
    OceanSim ocean;
    Renderer renderer;

    // --- orbit camera ---
    Camera camera;
    bool   mouse_dragging = false;
    double last_mouse_x   = 0.0;
    double last_mouse_y   = 0.0;

    // --- current foam read index (set by ocean.tick each frame) ---
    int foam_idx = 0;

    // --- ImGui UI panels ---
    std::vector<std::function<void()>> ui_panels;

private:
    wgpu::TextureView    get_next_surface_view();
    wgpu::RequiredLimits get_required_limits(wgpu::Adapter adapter) const;

    void render_ui(wgpu::RenderPassEncoder pass);

    static void on_mouse_button(GLFWwindow* w, int button, int action, int mods);
    static void on_cursor_pos(GLFWwindow* w, double x, double y);
    static void on_scroll(GLFWwindow* w, double dx, double dy);

public:
    Application(int width, int height);
    ~Application();

    void main_loop();
    bool is_running();
};
