#pragma once

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
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
	wgpu::RenderPipeline pipeline;
	wgpu::Buffer pointBuffer;
	wgpu::Buffer indexBuffer;
	wgpu::Buffer uniformBuffer;
	uint32_t indexCount;
	wgpu::BindGroup bindGroup;
	wgpu::PipelineLayout layout;
	wgpu::BindGroupLayout bindGroupLayout;

private:
    wgpu::TextureView GetNextSurfaceTextureView();

	void CreateGeometry(int size , std::vector<float>& pointData, std::vector<uint16_t>& indexData);

    void InitPipeline();
    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;
    void InitBuffers();
	void InitBindGroups();
public:
    Application(int width, int height);
    ~Application();

    void MainLoop();

    bool IsRunning();


};