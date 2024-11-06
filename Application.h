#pragma once

#define WEBGPU_CPP_IMPLEMENTATION

// #include <webgpu/webgpu.h>
#include "webgpu/webgpu.hpp"
#include "webgpu-utils.h"
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <iostream>
#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif // __EMSCRIPTEN__

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU


class Application
{
    GLFWwindow *window;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
    std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
public:
    Application(int width, int height);
    ~Application();

    void MainLoop();

    bool IsRunning();

private:
    wgpu::TextureView GetNextSurfaceTextureView();

};