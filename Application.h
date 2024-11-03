#pragma once

#include "webgpu-utils.h"
#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <iostream>
#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif // __EMSCRIPTEN__


class Application
{
    GLFWwindow *window;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
public:
    Application(int width, int height);
    ~Application();

    void MainLoop();

    bool IsRunning();

};