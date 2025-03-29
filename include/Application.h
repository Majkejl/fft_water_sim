#pragma once

// #include <webgpu/webgpu.h>
#include "webgpu/webgpu.hpp"
#include "webgpu-utils.h"
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cassert>
#include <vector>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif // __EMSCRIPTEN__

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU


struct MyUniforms  // TODO: split perhaps into separate structs
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 projection;
	glm::vec3 eye_pos; float _pad1;

	// TODO: add light maybe?
};

struct c_Uniforms
{
	float time; float _pad[3];
};

class Application
{
	// Uniforms
	MyUniforms uniforms;
	c_Uniforms c_uniforms;

	// No touchy here
	uint32_t width, height;
    GLFWwindow *window;
	wgpu::Device device;
	wgpu::Queue queue;
	wgpu::Surface surface;
	std::unique_ptr<wgpu::ErrorCallback> uncapturedErrorCallbackHandle;
	wgpu::TextureFormat surfaceFormat = wgpu::TextureFormat::Undefined;
	wgpu::TextureView depthTextureView;
	wgpu::Texture  depthTexture;
	wgpu::RenderPipeline pipeline;
	wgpu::ComputePipeline compPipeline;
	wgpu::Buffer pointBuffer;
	wgpu::Buffer indexBuffer;
	wgpu::Buffer uniformBuffer;
	uint32_t indexCount;
	uint32_t vertexCount;
	wgpu::BindGroup bindGroup;
	wgpu::PipelineLayout layout;
	wgpu::BindGroupLayout bindGroupLayout;
	
	wgpu::BindGroup c_bindGroup;
	wgpu::PipelineLayout c_layout;
	wgpu::BindGroupLayout c_bindGroupLayout;


	wgpu::Texture heightTexture;
	wgpu::TextureView heightTextureView;
	wgpu::Texture spectrumTexture;
	wgpu::TextureView spectrumTextureView;
	wgpu::Sampler sampler;

private:
    wgpu::TextureView GetNextSurfaceTextureView();
    wgpu::RequiredLimits GetRequiredLimits(wgpu::Adapter adapter) const;
	void InitCompute();
    void InitPipeline();
    void InitBuffers();
	void InitBindGroups();
	void InitTextures();
public:
    Application(int width, int height);
    ~Application();

    void MainLoop();
	void RunCompute();

    bool IsRunning();


};