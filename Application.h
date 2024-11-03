#pragma once

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
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
public:
    Application();
    ~Application();

    void MainLoop();

    bool IsRunning();

};