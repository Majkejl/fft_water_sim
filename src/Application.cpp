
#define WEBGPU_CPP_IMPLEMENTATION

#include "Application.h"
#include "ResourceManager.h"
// #include "PerlinNoise.hpp"
#include <random>
#include <cmath>
#include <cstring>
#include <numbers>

using namespace wgpu;

#define MESH_SIZE 150
#define TEXTURE_SIZE 256
#define TEXTURE_LOG 8
#define PATCH_SIZE 150

#ifdef __EMSCRIPTEN__
#define ALIGNMENT 255
#else
#define ALIGNMENT 31
#endif

// Creator funcs
namespace {
	void CreateGeometry(int size, std::vector<float>& pointData, std::vector<uint16_t>& indexData)
	{
		pointData.clear();
		indexData.clear();

		for (int i = 0; i < size; i++)
		{
			for (int j = 0; j < size; j++)
			{
				pointData.push_back(static_cast<float>(j) / (size - 1)); // x
				pointData.push_back(static_cast<float>(i) / (size - 1)); // y
				pointData.push_back(0.f);		   				         // z

				pointData.push_back(static_cast<float>(j) / (size - 1)); // u
				pointData.push_back(static_cast<float>(i) / (size - 1)); // v

				if (i < size - 1 && j < size - 1)
				{
					indexData.push_back(static_cast<uint16_t>(j + i * size));
					indexData.push_back(static_cast<uint16_t>(j + i * size + size));
					indexData.push_back(static_cast<uint16_t>(j + i * size + 1));
					indexData.push_back(static_cast<uint16_t>(j + i * size + 1));
					indexData.push_back(static_cast<uint16_t>(j + i * size + size));
					indexData.push_back(static_cast<uint16_t>(j + i * size + size + 1));
				}
			}
		}
	}

	// void CreateHeightMap(int size, std::vector<uint8_t>& heightMap)
	// {
	// 	heightMap.clear();
		
	// 	const siv::PerlinNoise::seed_type seed = { 123456u }; 
	// 	const siv::PerlinNoise perlin{ seed };
	// 	const float freq = 8.f / TEXTURE_SIZE;

	// 	for (int i = 0; i < size; i++)
	// 	{
	// 		for (int j = 0; j < size; j++) 
	// 		{
	// 			heightMap.push_back( static_cast<uint8_t>(255 * perlin.octave2D_01(j * freq, i * freq, 4)) );
	// 			heightMap.push_back( 0 );
	// 			heightMap.push_back( 0 );
	// 			heightMap.push_back( 0 );
	// 		}
	// 	}
	// }
	
	double jonswap(double pos_x, double pos_y, double fetch, double wind_x, double wind_y, double enhancment = 3.3)
	{
		using std::pow;
		using std::exp;

		auto freq = std::sqrt(pos_x * pos_x + pos_y * pos_y);
		if (freq < 0.001) return 0.;
		auto wind = std::sqrt(wind_x * wind_x + wind_y * wind_y);

		double g = 9.81;
		double freq_p = 22 * pow( pow(g, 2) / (fetch * wind), 1.0 / 3.0);
		double alpha = 0.076 * pow( pow(wind, 2) / (fetch * g), 0.22);
		double sigma = ( freq <= freq_p ) ? 0.07 : 0.09;
		double r = exp( -pow(freq - freq_p, 2) / ( 2 * pow(sigma * freq_p, 2)));

		double density = alpha * ( pow(g, 2) / pow(freq, 5) ) * exp( -5 * pow(freq_p / freq, 4) / 4) * pow(enhancment, r);
		
		return density; //* std::pow(std::max((pos_x / freq) * (wind_x / wind) + (pos_y / freq) * (wind_y / wind), 0.), 2);
	}

	void fillTextures(std::vector<float>& spectrum, std::vector<float>& k_data) // TODO add params
	{
		int N = TEXTURE_SIZE;
		spectrum = std::vector<float>(N * N * 2);
		k_data = std::vector<float>(N * N * 4);
		std::random_device rd{};
		std::mt19937 gen{rd()};
		std::normal_distribution d{0., 1.};
		auto rand_num = [&d, &gen]{ return d(gen); };

		for (int ky = 0; ky < N; ky++) {
			for (int kx = 0; kx < N; kx++) {
				
				int i = kx + ky * N;
				
				int sx = (N - kx) % N;
				int sy = (N - ky) % N;
				int j  = sx + sy * N;

				int kx_m = (kx <= N/2) ? kx : kx - N;
				int ky_m = (ky <= N/2) ? ky : ky - N;
				
				// physical wave vector
				auto pos = [&](int x){ return 2 * static_cast<float>(x) * static_cast<float>(std::numbers::pi) / PATCH_SIZE; };
				float kx_phys = pos(kx_m);
				float ky_phys = pos(ky_m);

				float k_len = std::sqrt(kx_phys * kx_phys + ky_phys * ky_phys);

				float omega = std::sqrt(9.81f * k_len);

				// store k data
				k_data[4 * i + 0] = kx_phys;
				k_data[4 * i + 1] = ky_phys;
				k_data[4 * i + 2] = omega;
				k_data[4 * i + 3] = 0.;

				// mirrored pair gets mirrored k
				k_data[4 * j + 0] = -kx_phys;
				k_data[4 * j + 1] = -ky_phys;
				k_data[4 * j + 2] = omega;
				k_data[4 * j + 3] = 0.;

				auto P = jonswap(kx_phys, ky_phys, 5000., 40., 0.);


				auto gr = rand_num();
				auto gi = rand_num();

				auto scale = sqrt(P * 0.5f);

				float re = static_cast<float>(gr * scale);
				float im = static_cast<float>(gi * scale);


				// Self-symmetric (Nyquist / DC cases)
				if (i == j) {
					spectrum[2*i] = re;
					spectrum[2*i + 1] = 0.0f;
				}
				else {
					spectrum[2*i]     = re;
					spectrum[2*i + 1] = im;

					spectrum[2*j]     = re;
					spectrum[2*j + 1] = -im;
				}
			}
		}
	}

	// std::pair<float, float> w_k(int k)
	// {
	// 	return {static_cast<float>(std::cos(2 * std::numbers::pi / TEXTURE_SIZE * k)),
	// 				static_cast<float>(-std::sin(2 * std::numbers::pi / TEXTURE_SIZE * k))};
	// }
}

Application::Application(int w, int h) : width(w), height(h) // TODO : add throws on errors
{
	// Open window
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow(width, height, "Learn WebGPU", nullptr, nullptr);
	
	Instance instance = wgpuCreateInstance(nullptr);
	
	// Get adapter
	std::cout << "Requesting adapter..." << std::endl;
	surface = glfwGetWGPUSurface(instance, window);
	RequestAdapterOptions adapterOpts = {};
	adapterOpts.compatibleSurface = surface;
	Adapter adapter = instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;
	
	instance.release();
	

	std::cout << "Requesting device..." << std::endl;
	DeviceDescriptor deviceDesc = {};
	deviceDesc.label = "My Device";

	WGPUFeatureName features[] = {
		WGPUFeatureName_Float32Filterable,
	};

	deviceDesc.requiredFeatureCount = 1;
	deviceDesc.requiredFeatures = features;
	deviceDesc.requiredLimits = nullptr;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = "The default queue";
	deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	};
	RequiredLimits requiredLimits = GetRequiredLimits(adapter);
	deviceDesc.requiredLimits = &requiredLimits;
	device = adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << device << std::endl;
	
	// Device error callback
	uncapturedErrorCallbackHandle = device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
		std::cout << "Uncaptured device error: type " << type;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
	});
	
	queue = device.getQueue();

	// Configure the surface
	SurfaceConfiguration config = {};
	
	// Configuration of the textures created for the underlying swap chain
	config.width = width;
	config.height = height;
	config.usage = TextureUsage::RenderAttachment;
	surfaceFormat = surface.getPreferredFormat(adapter);
	config.format = surfaceFormat;

	// And we do not need any particular view format:
	config.viewFormatCount = 0;
	config.viewFormats = nullptr;
	config.device = device;
	config.presentMode = PresentMode::Fifo;
	config.alphaMode = CompositeAlphaMode::Auto;

	surface.configure(config);

	// Release the adapter only after it has been fully utilized
	adapter.release();

	InitPipeline();
	InitCompute();
	InitBuffers();
	InitTextures();
	InitBindGroups();
}

Application::~Application() // TODO: tidy up
{
	//
	heightTextureView1.release();
	heightTextureView2.release();
	heightTexture1.destroy();
	heightTexture2.destroy();
	heightTexture1.release();
	heightTexture2.release();
	bindGroup.release();
	layout.release();
	bindGroupLayout.release();
	c_layout.release();
	c_bindGroupLayout.release();
	uniformBuffer.release();
	pointBuffer.release();
	indexBuffer.release();
	depthTextureView.release();
	depthTexture.destroy();
	depthTexture.release();
	pipeline.release();
	compPipeline.release();
	
	// core elements
	surface.unconfigure();
	queue.release();
	surface.release();
	device.release();
	glfwDestroyWindow(window);
	glfwTerminate();
}

void Application::MainLoop() 
{
	glfwPollEvents();

	RunCompute();

	// Update uniform buffer
	uniforms.eye_pos = glm::vec3(2.f);
	uniforms.projection = glm::perspective(glm::radians(45.0f), static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.0f);
	uniforms.view = glm::lookAt(uniforms.eye_pos, glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f));
	uniforms.model = glm::scale(glm::translate(glm::mat4(1.f), glm::vec3(-1.f, -1.f, 0.f)),
								glm::vec3(2.f, 2.f, 1.f));
	queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));

	// Get the next target texture view
	TextureView targetView = GetNextSurfaceTextureView();
	if (!targetView) return;

	// Create a command encoder for the draw call
	CommandEncoderDescriptor encoderDesc = {};
	encoderDesc.label = "My command encoder";
	CommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	// Create the render pass that clears the screen with our color
	RenderPassDescriptor renderPassDesc = {};

	// The attachment part of the render pass descriptor describes the target texture of the pass
	RenderPassColorAttachment renderPassColorAttachment = {};
	renderPassColorAttachment.view = targetView;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = LoadOp::Clear;
	renderPassColorAttachment.storeOp = StoreOp::Store;
	renderPassColorAttachment.clearValue = WGPUColor{ 0.05, 0.05, 0.05, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
 	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // NOT WEBGPU_BACKEND_WGPU

	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	// We now add a depth/stencil attachment:
	RenderPassDepthStencilAttachment depthStencilAttachment;
	// The view of the depth texture
	depthStencilAttachment.view = depthTextureView;

	// The initial value of the depth buffer, meaning "far"
	depthStencilAttachment.depthClearValue = 1.0f;
	// Operation settings comparable to the color attachment
	depthStencilAttachment.depthLoadOp = LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = StoreOp::Store;
	// we could turn off writing to the depth buffer globally here
	depthStencilAttachment.depthReadOnly = false;

	// Stencil setup, mandatory but unused
	depthStencilAttachment.stencilClearValue = 0;

#ifdef WEBGPU_BACKEND_WGPU
	depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
	depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
	depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
#endif
	depthStencilAttachment.stencilReadOnly = true;

	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;

	renderPassDesc.timestampWrites = nullptr;

	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	// Select which render pipeline to use
	renderPass.setPipeline(pipeline);

	// Set vertex buffer while encoding the render pass
	renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());
	
	// The second argument must correspond to the choice of uint16_t or uint32_t
	// we've done when creating the index buffer.
	renderPass.setIndexBuffer(indexBuffer, IndexFormat::Uint16, 0, indexBuffer.getSize());

	// Set binding group here!
	renderPass.setBindGroup(0, bindGroup, 0, nullptr);

	// Replace `draw()` with `drawIndexed()` and `vertexCount` with `indexCount`
	// The extra argument is an offset within the index buffer.
	renderPass.drawIndexed(indexCount, 1, 0, 0, 0);
	//renderPass.draw(50, 1, 0, 0);

	renderPass.end();
	renderPass.release();

	// Finally encode and submit the render pass
	CommandBufferDescriptor cmdBufferDescriptor = {};
	cmdBufferDescriptor.label = "Command buffer";
	CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	encoder.release();
	//std::cout << "Submitting command..." << std::endl;
	queue.submit(1, &command);
	command.release();
	//std::cout << "Command submitted." << std::endl;

	// At the end of the frame
	targetView.release();
#ifndef __EMSCRIPTEN__
	surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
	device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
	device.poll(false);
#endif
}

bool Application::IsRunning() 
{
	return !glfwWindowShouldClose(window);
}

TextureView Application::GetNextSurfaceTextureView() 
{
	// Get the surface texture
	SurfaceTexture surfaceTexture;
	surface.getCurrentTexture(&surfaceTexture);
	if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success) {
		return nullptr;
	}
	Texture texture = surfaceTexture.texture;

	// Create a view for this surface texture
	TextureViewDescriptor viewDescriptor;
	viewDescriptor.label = "Surface texture view";
	viewDescriptor.format = texture.getFormat();
	viewDescriptor.dimension = TextureViewDimension::_2D;
	viewDescriptor.baseMipLevel = 0;
	viewDescriptor.mipLevelCount = 1;
	viewDescriptor.baseArrayLayer = 0;
	viewDescriptor.arrayLayerCount = 1;
	viewDescriptor.aspect = TextureAspect::All;
	TextureView targetView = texture.createView(viewDescriptor);

#ifndef WEBGPU_BACKEND_WGPU
	// We no longer need the texture, only its view
	// (NB: with wgpu-native, surface textures must not be manually released)
	wgpuTextureRelease(surfaceTexture.texture);
#endif // WEBGPU_BACKEND_WGPU

	return targetView;
}

void Application::InitPipeline() 
{
	std::cout << "Creating shader module..." << std::endl;
	ShaderModule shaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", device);
	std::cout << "Shader module: " << shaderModule << std::endl;

	// Check for errors
	if (shaderModule == nullptr) {
		std::cerr << "Could not load shader!" << std::endl;
		exit(1);
	}

	// Create the render pipeline
	RenderPipelineDescriptor pipelineDesc;

	// Configure the vertex pipeline
	// We use one vertex buffer
	VertexBufferLayout vertexBufferLayout;
	// We now have 2 attributes
	std::vector<VertexAttribute> vertexAttribs(2);
	
	// Describe the position attribute
	vertexAttribs[0].shaderLocation = 0; // @location(0)
	vertexAttribs[0].format = VertexFormat::Float32x2;
	vertexAttribs[0].offset = 0;

	vertexAttribs[1].shaderLocation = 1;
	vertexAttribs[1].format = VertexFormat::Float32x2;
	vertexAttribs[1].offset = 3 * sizeof(float);
	
	vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
	vertexBufferLayout.attributes = vertexAttribs.data();
	
	vertexBufferLayout.arrayStride = 5 * sizeof(float);

	vertexBufferLayout.stepMode = VertexStepMode::Vertex;
	
	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;

	// NB: We define the 'shaderModule' in the second part of this chapter.
	// Here we tell that the programmable vertex shader stage is described
	// by the function called 'vs_main' in that module.
	pipelineDesc.vertex.module = shaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	// Each sequence of 3 vertices is considered as a triangle
	pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
	
	// We'll see later how to specify the order in which vertices should be
	// connected. When not specified, vertices are considered sequentially.
	pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
	
	// The face orientation is defined by assuming that when looking
	// from the front of the face, its corner vertices are enumerated
	// in the counter-clockwise (CCW) order.
	pipelineDesc.primitive.frontFace = FrontFace::CCW;
	
	// But the face orientation does not matter much because we do not
	// cull (i.e. "hide") the faces pointing away from us (which is often
	// used for optimization).
	pipelineDesc.primitive.cullMode = CullMode::None;

	// We tell that the programmable fragment shader stage is described
	// by the function called 'fs_main' in the shader module.
	FragmentState fragmentState;
	fragmentState.module = shaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;

	BlendState blendState;
	blendState.color.srcFactor = BlendFactor::SrcAlpha;
	blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = BlendOperation::Add;
	blendState.alpha.srcFactor = BlendFactor::Zero;
	blendState.alpha.dstFactor = BlendFactor::One;
	blendState.alpha.operation = BlendOperation::Add;
	
	ColorTargetState colorTarget;
	colorTarget.format = surfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = ColorWriteMask::All; // We could write to only some of the color channels.
	
	// We have only one target because our render pass has only one output color
	// attachment.
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;

	DepthStencilState depthStencilState = Default;
	depthStencilState.depthCompare = CompareFunction::Less;
	depthStencilState.depthWriteEnabled = true;
	// Store the format in a variable as later parts of the code depend on it
	TextureFormat depthTextureFormat = TextureFormat::Depth24Plus;
	depthStencilState.format = depthTextureFormat;

	// Deactivate the stencil alltogether
	depthStencilState.stencilReadMask = 0;
	depthStencilState.stencilWriteMask = 0;

	pipelineDesc.depthStencil = &depthStencilState;

	pipelineDesc.multisample.count = 1;	
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	// Create the depth texture
	TextureDescriptor depthTextureDesc;
	depthTextureDesc.dimension = TextureDimension::_2D;
	depthTextureDesc.format = depthTextureFormat;
	depthTextureDesc.mipLevelCount = 1;
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.size = {width, height, 1};
	depthTextureDesc.usage = TextureUsage::RenderAttachment;
	depthTextureDesc.viewFormatCount = 1;
	depthTextureDesc.viewFormats = (WGPUTextureFormat*)&depthTextureFormat;
	depthTexture = device.createTexture(depthTextureDesc);
	
	// Create the view of the depth texture manipulated by the rasterizer
	TextureViewDescriptor depthTextureViewDesc;
	depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
	depthTextureViewDesc.baseArrayLayer = 0;
	depthTextureViewDesc.arrayLayerCount = 1;
	depthTextureViewDesc.baseMipLevel = 0;
	depthTextureViewDesc.mipLevelCount = 1;
	depthTextureViewDesc.dimension = TextureViewDimension::_2D;
	depthTextureViewDesc.format = depthTextureFormat;
	depthTextureView = depthTexture.createView(depthTextureViewDesc);


	// Samples per pixel
	pipelineDesc.multisample.count = 1;

	// Default value for the mask, meaning "all bits on"
	pipelineDesc.multisample.mask = ~0u;

	// Default value as well (irrelevant for count = 1 anyways)
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	std::vector<BindGroupLayoutEntry> bindingLayoutEntries(3, Default);

	// The uniform buffer binding that we already had
	BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
	bindingLayout.binding = 0;
	bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
	bindingLayout.buffer.type = BufferBindingType::Uniform;
	bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

	// The texture binding
	BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
	textureBindingLayout.binding = 1;
	textureBindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
	textureBindingLayout.texture.sampleType = TextureSampleType::Float;
	textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	// The texture sampler binding
	BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
	samplerBindingLayout.binding = 2;
	samplerBindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
	samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

	// Create a bind group layout
	BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
	bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
	bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

	// Create the pipeline layout
	PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&bindGroupLayout;
	layout = device.createPipelineLayout(layoutDesc);

	pipelineDesc.layout = layout;
	
	pipeline = device.createRenderPipeline(pipelineDesc);

	// We no longer need to access the shader module
	shaderModule.release();
}

void Application::InitCompute()
{
	// Load compute shader
	ShaderModule computeShaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/compute.wgsl", device);

	// Create compute pipeline layout

	std::vector<BindGroupLayoutEntry> bindingLayoutEntries(6, Default);

	BindGroupLayoutEntry& uniformBindingLayout = bindingLayoutEntries[0];
	uniformBindingLayout.binding = 0;
	uniformBindingLayout.visibility = ShaderStage::Compute;
	uniformBindingLayout.buffer.type = BufferBindingType::Uniform;
	uniformBindingLayout.buffer.hasDynamicOffset = true;
	uniformBindingLayout.buffer.minBindingSize = sizeof(c_Uniforms);

	BindGroupLayoutEntry& textureOutBindingLayout = bindingLayoutEntries[1];
	textureOutBindingLayout.binding = 1;
	textureOutBindingLayout.visibility = ShaderStage::Compute;
	textureOutBindingLayout.storageTexture.access = StorageTextureAccess::WriteOnly;
	textureOutBindingLayout.storageTexture.viewDimension = TextureViewDimension::_2D;
	textureOutBindingLayout.storageTexture.format = TextureFormat::RGBA32Float;

	BindGroupLayoutEntry& textureInBindingLayout = bindingLayoutEntries[2];
	textureInBindingLayout.binding = 3;
	textureInBindingLayout.visibility = ShaderStage::Compute;
	textureInBindingLayout.texture.sampleType = TextureSampleType::UnfilterableFloat;
	textureInBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	BindGroupLayoutEntry& spectrumBindingLayout = bindingLayoutEntries[3];
	spectrumBindingLayout.binding = 4;
	spectrumBindingLayout.visibility = ShaderStage::Compute;
	spectrumBindingLayout.texture.sampleType = TextureSampleType::UnfilterableFloat;
	spectrumBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	BindGroupLayoutEntry& butterflyBindingLayout = bindingLayoutEntries[4];
	butterflyBindingLayout.binding = 5;
	butterflyBindingLayout.visibility = ShaderStage::Compute;
	butterflyBindingLayout.texture.sampleType = TextureSampleType::UnfilterableFloat;
	butterflyBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	BindGroupLayoutEntry& kDataBindingLayout = bindingLayoutEntries[5];
	kDataBindingLayout.binding = 6;
	kDataBindingLayout.visibility = ShaderStage::Compute;
	kDataBindingLayout.texture.sampleType = TextureSampleType::UnfilterableFloat;
	kDataBindingLayout.texture.viewDimension = TextureViewDimension::_2D;


	// Create a bind group layout
	BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
	bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
	c_bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

	PipelineLayoutDescriptor pipelineLayoutDesc;
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&c_bindGroupLayout;
	c_layout = device.createPipelineLayout(pipelineLayoutDesc);

	// Create compute pipeline
	ComputePipelineDescriptor computePipelineDesc;
	computePipelineDesc.compute.constantCount = 0;
	computePipelineDesc.compute.constants = nullptr;
	computePipelineDesc.compute.entryPoint = "cs_main";
	computePipelineDesc.compute.module = computeShaderModule;
	computePipelineDesc.layout = c_layout;
	compPipeline = device.createComputePipeline(computePipelineDesc);
	
	computePipelineDesc.compute.entryPoint = "timeSpectrum";
	time_spectrum_pipe = device.createComputePipeline(computePipelineDesc);

	computePipelineDesc.compute.entryPoint = "fft_horizontal";
	fft_horizontal_pipe = device.createComputePipeline(computePipelineDesc);

	computePipelineDesc.compute.entryPoint = "fft_vertical";
	fft_vertical_pipe = device.createComputePipeline(computePipelineDesc);

}

void Application::RunCompute()
{
	// Pre-fill all TEXTURE_LOG stage slots in c_uniformBuffer before encoding.
	// Each slot is at offset s * c_uniformStride; dynamic offsets select the right slot per dispatch.
	float currentTime = static_cast<float>(glfwGetTime());
	std::vector<uint8_t> ubuf(c_uniformStride * TEXTURE_LOG, 0);
	for (int s = 0; s < TEXTURE_LOG; s++) {
		c_Uniforms cu{ currentTime, static_cast<unsigned>(s), TEXTURE_SIZE, 0.f };
		memcpy(ubuf.data() + s * c_uniformStride, &cu, sizeof(c_Uniforms));
	}
	queue.writeBuffer(c_uniformBuffer, 0, ubuf.data(), ubuf.size());

    CommandEncoderDescriptor encoderDesc = Default;
    CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

	ComputePassDescriptor computePassDesc;
	computePassDesc.timestampWrites = nullptr;
	ComputePassEncoder computePass = encoder.beginComputePass(computePassDesc);

	// Step 1: timeSpectrum → heightTexture1 (c_bindGroup1: out=1, in=2)
	uint32_t offset0 = 0;
	computePass.setPipeline(time_spectrum_pipe);
	computePass.setBindGroup(0, c_bindGroup1, 1, &offset0);
	computePass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 32, 1);

	// Step 2: H-FFT, 8 stages ping-pong.
	// Even stage → c_bindGroup2 (read from 1, write to 2)
	// Odd  stage → c_bindGroup1 (read from 2, write to 1)
	// After stage 7 (odd): result in heightTexture1
	computePass.setPipeline(fft_horizontal_pipe);
	for (int s = 0; s < TEXTURE_LOG; s++) {
		uint32_t offset = static_cast<uint32_t>(s) * c_uniformStride;
		BindGroup& bg = (s % 2 == 0) ? c_bindGroup2 : c_bindGroup1;
		computePass.setBindGroup(0, bg, 1, &offset);
		computePass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 32, TEXTURE_SIZE / 32, 1);
	}

	// Step 3: V-FFT, same ping-pong pattern. Result in heightTexture1 after stage 7.
	computePass.setPipeline(fft_vertical_pipe);
	for (int s = 0; s < TEXTURE_LOG; s++) {
		uint32_t offset = static_cast<uint32_t>(s) * c_uniformStride;
		BindGroup& bg = (s % 2 == 0) ? c_bindGroup2 : c_bindGroup1;
		computePass.setBindGroup(0, bg, 1, &offset);
		computePass.dispatchWorkgroups(TEXTURE_SIZE / 32, TEXTURE_SIZE / 2 / 32, 1);
	}

	computePass.end();
#ifndef WEBGPU_BACKEND_WGPU
    wgpuComputePassEncoderRelease(computePass);
#endif

    CommandBuffer commands = encoder.finish(CommandBufferDescriptor{});
    queue.submit(commands);

#ifndef WEBGPU_BACKEND_WGPU
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
#endif
}

RequiredLimits Application::GetRequiredLimits(Adapter adapter) const 
{
	// Get adapter supported limits, in case we need them
	SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);

	// Don't forget to = Default
	RequiredLimits requiredLimits = Default;

	requiredLimits.limits.maxVertexAttributes = 2;
	requiredLimits.limits.maxVertexBuffers = 1;
	requiredLimits.limits.maxBufferSize = MESH_SIZE * MESH_SIZE * 5 * sizeof(float);
	requiredLimits.limits.maxVertexBufferArrayStride = 5 * sizeof(float);

	// There is a maximum of 3 float forwarded from vertex to fragment shader
	//requiredLimits.limits.maxInterStageShaderComponents = 6;

	// We use at most 1 bind group for now
	requiredLimits.limits.maxBindGroups = 1;
	// We use at most 1 uniform buffer per stage
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
	// Uniform structs have a size of maximum 16 float (more than what we need)
	requiredLimits.limits.maxUniformBufferBindingSize = sizeof(MyUniforms) + sizeof(c_Uniforms);

	// For the depth buffer, we enable textures (up to the size of the window):
	requiredLimits.limits.maxTextureDimension1D = height;
	requiredLimits.limits.maxTextureDimension2D = width;
	requiredLimits.limits.maxTextureArrayLayers = 1;

	// Add the possibility to sample a texture in a shader
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 4;

	requiredLimits.limits.maxSamplersPerShaderStage = 1;

	// These two limits are different because they are "minimum" limits,
	// they are the only ones we are may forward from the adapter's supported
	// limits.
	requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

	requiredLimits.limits.maxComputeWorkgroupSizeX = 1024;
	requiredLimits.limits.maxComputeWorkgroupSizeY = 32;
	requiredLimits.limits.maxComputeWorkgroupSizeZ = 1;
	requiredLimits.limits.maxComputeInvocationsPerWorkgroup = 1024;
	return requiredLimits;
}

void Application::InitBuffers() 
{
	// Define data vectors, but without filling them in
	std::vector<float> pointData;
	std::vector<uint16_t> indexData;

	// Here we use the new 'loadGeometry' function:
	//bool success = ResourceManager::loadGeometry(RESOURCE_DIR "/webgpu.txt", pointData, indexData);

	// Check for errors
	//if (!success) {
	//	std::cerr << "Could not load geometry!" << std::endl;
	//	exit(1);
	//}

	CreateGeometry(MESH_SIZE, pointData, indexData);

	// We now store the index count rather than the vertex count
	indexCount = static_cast<uint32_t>(indexData.size());
	vertexCount = static_cast<uint32_t>(pointData.size());
	
	// Create vertex buffer
	BufferDescriptor bufferDesc;
	bufferDesc.size = pointData.size() * sizeof(float);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
	bufferDesc.mappedAtCreation = false;
	pointBuffer = device.createBuffer(bufferDesc);
	
	// Upload geometry data to the buffer
	queue.writeBuffer(pointBuffer, 0, pointData.data(), bufferDesc.size);

	// Create index buffer
	// (we reuse the bufferDesc initialized for the pointBuffer)
	bufferDesc.size = indexData.size() * sizeof(uint16_t);
	bufferDesc.size = (bufferDesc.size + 3) & ~3; // round up to the next multiple of 4
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
	indexBuffer = device.createBuffer(bufferDesc);

	queue.writeBuffer(indexBuffer, 0, indexData.data(), bufferDesc.size);

	// Create uniform buffer (reusing bufferDesc from other buffer creations)
	// The buffer will only contain 1 float with the value of uTime
	// then 3 floats left empty but needed by alignment constraints
	bufferDesc.size = ((sizeof(MyUniforms) + ALIGNMENT) & ~ALIGNMENT) + sizeof(c_Uniforms);

	// Make sure to flag the buffer as BufferUsage::Uniform
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;

	bufferDesc.mappedAtCreation = false;
	uniformBuffer = device.createBuffer(bufferDesc);

	// Upload uniform data
	uniforms.model = glm::mat4(1.f);
	uniforms.view = glm::mat4(1.f);
	uniforms.projection = glm::mat4(1.f);
	queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));

	// Compute uniform buffer: TEXTURE_LOG slots at 256-byte stride for dynamic offsets
	c_uniformStride = (sizeof(c_Uniforms) + 255u) & ~255u;  // align to 256 (WebGPU min)
	bufferDesc.size = c_uniformStride * TEXTURE_LOG;
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	c_uniformBuffer = device.createBuffer(bufferDesc);
}

void Application::InitBindGroups() 
{
	std::vector<BindGroupEntry> bindings(3);

	bindings[0].binding = 0;
	bindings[0].buffer = uniformBuffer;
	bindings[0].offset = 0;
	bindings[0].size = sizeof(MyUniforms);

	bindings[1].binding = 1;
	bindings[1].textureView = heightTextureView1;

	bindings[2].binding = 2;
	bindings[2].sampler = sampler;

	BindGroupDescriptor bindGroupDesc;
	bindGroupDesc.layout = bindGroupLayout;
	bindGroupDesc.entryCount = (uint32_t)bindings.size();
	bindGroupDesc.entries = bindings.data();
	bindGroup = device.createBindGroup(bindGroupDesc);
	
	// compute
	
	bindings.clear();
	bindings.emplace_back();
	bindings[0].binding = 0;
	bindings[0].buffer = c_uniformBuffer;
	bindings[0].offset = 0;
	bindings[0].size = sizeof(c_Uniforms);
	
	
	bindings.emplace_back();
	bindings[1].binding = 1;
	bindings[1].textureView = heightTextureView1;
	
	bindings.emplace_back();
	bindings[2].binding = 3;
	bindings[2].textureView = heightTextureView2;
	
	bindings.emplace_back();
	bindings[3].binding = 4;
	bindings[3].textureView = spectrumTextureView;
	
	bindings.emplace_back();
	bindings[4].binding = 5;
	bindings[4].textureView = butterflyTextureView;

	bindings.emplace_back();
	bindings[5].binding = 6;
	bindings[5].textureView = kDataTextureView;
	
	BindGroupDescriptor c_bindGroupDesc;
	c_bindGroupDesc.layout = c_bindGroupLayout;
	c_bindGroupDesc.entryCount = (uint32_t)bindings.size();
	c_bindGroupDesc.entries = bindings.data();
	c_bindGroup1 = device.createBindGroup(c_bindGroupDesc);

	bindings[1].textureView = heightTextureView2;
	bindings[2].textureView = heightTextureView1;
	c_bindGroupDesc.entries = bindings.data();
	c_bindGroup2 = device.createBindGroup(c_bindGroupDesc);

}

void Application::InitTextures() 
{
	// fft ping pongs
	TextureDescriptor textureDesc;
	textureDesc.dimension = TextureDimension::_2D;
	textureDesc.size = { TEXTURE_SIZE, TEXTURE_SIZE, 1 };
	textureDesc.mipLevelCount = 1;
	textureDesc.sampleCount = 1;
	textureDesc.format = TextureFormat::RGBA32Float;
	textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::StorageBinding;
	textureDesc.viewFormatCount = 0;
	textureDesc.viewFormats = nullptr;

	heightTexture1 = device.createTexture(textureDesc);
	heightTexture2 = device.createTexture(textureDesc);

	
	TextureViewDescriptor textureViewDesc;
	textureViewDesc.aspect = TextureAspect::All;
	textureViewDesc.baseArrayLayer = 0;
	textureViewDesc.arrayLayerCount = 1;
	textureViewDesc.baseMipLevel = 0;
	textureViewDesc.mipLevelCount = 1;
	textureViewDesc.dimension = TextureViewDimension::_2D;
	textureViewDesc.format = textureDesc.format;
	heightTextureView1 = heightTexture1.createView(textureViewDesc);
	heightTextureView2 = heightTexture2.createView(textureViewDesc);
	
	// h0 spectrum
	textureDesc.format = TextureFormat::RG32Float;
	textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
	spectrumTexture = device.createTexture(textureDesc);

	textureViewDesc.format = textureDesc.format;
	spectrumTextureView = spectrumTexture.createView(textureViewDesc);
	
	// k_data
	textureDesc.format = TextureFormat::RGBA32Float;
	kDataTexture = device.createTexture(textureDesc);
	textureViewDesc.format = textureDesc.format;
	kDataTextureView = kDataTexture.createView(textureViewDesc);
	
	std::vector<float> spectrum;
	std::vector<float> k_data;
	fillTextures(spectrum, k_data);

	ImageCopyTexture destination;
	destination.texture = spectrumTexture;
	destination.mipLevel = 0;
	destination.origin = { 0, 0, 0 };
	destination.aspect = TextureAspect::All;

	TextureDataLayout source;
	source.offset = 0;
	source.bytesPerRow = TEXTURE_SIZE * 2 * sizeof(float);
	source.rowsPerImage = TEXTURE_SIZE;

	queue.writeTexture(destination, spectrum.data(), spectrum.size() * sizeof(float), source, textureDesc.size);

	destination.texture = kDataTexture;
	source.bytesPerRow = TEXTURE_SIZE * 4 * sizeof(float);
	queue.writeTexture(destination, k_data.data(), k_data.size() * sizeof(float), source, textureDesc.size);

	// Butterfly texture: (N/2) x log2(N), RGBA32Float
	// Each texel (x, stage) = (tw.re, tw.im, a_idx, b_idx) for DIT FFT
	textureDesc.size = { TEXTURE_SIZE / 2, TEXTURE_LOG, 1 };
	textureDesc.format = TextureFormat::RGBA32Float;
	textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
	butterflyTexture = device.createTexture(textureDesc);
	textureViewDesc.format = textureDesc.format;
	butterflyTextureView = butterflyTexture.createView(textureViewDesc);

	{
		const float pi = static_cast<float>(std::numbers::pi);
		std::vector<float> bfly(TEXTURE_SIZE / 2 * TEXTURE_LOG * 4);
		for (int s = 0; s < TEXTURE_LOG; s++) {
			int halfSpan = 1 << s;
			int span     = 2 * halfSpan;
			for (int x = 0; x < TEXTURE_SIZE / 2; x++) {
				int local_j = x % halfSpan;
				int group   = x / halfSpan;
				int a_idx   = group * span + local_j;
				int b_idx   = a_idx + halfSpan;
				int k       = local_j * (TEXTURE_SIZE / span);
				float angle = +2.0f * pi * k / TEXTURE_SIZE;
				int i = (x + s * (TEXTURE_SIZE / 2)) * 4;
				bfly[i+0] = std::cos(angle);
				bfly[i+1] = std::sin(angle);
				bfly[i+2] = static_cast<float>(a_idx);
				bfly[i+3] = static_cast<float>(b_idx);
			}
		}
		ImageCopyTexture bflyDest = {};
		bflyDest.texture  = butterflyTexture;
		bflyDest.mipLevel = 0;
		bflyDest.origin   = { 0, 0, 0 };
		bflyDest.aspect   = TextureAspect::All;
		TextureDataLayout bflySrc = {};
		bflySrc.offset       = 0;
		bflySrc.bytesPerRow  = (TEXTURE_SIZE / 2) * 4 * sizeof(float);
		bflySrc.rowsPerImage = TEXTURE_LOG;
		queue.writeTexture(bflyDest, bfly.data(), bfly.size() * sizeof(float), bflySrc, textureDesc.size);
	}

	// Create a sampler
	SamplerDescriptor samplerDesc;
	samplerDesc.addressModeU = AddressMode::ClampToEdge;
	samplerDesc.addressModeV = AddressMode::ClampToEdge;
	samplerDesc.addressModeW = AddressMode::ClampToEdge;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 1.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;
	sampler = device.createSampler(samplerDesc);
}
