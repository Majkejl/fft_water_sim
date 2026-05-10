#pragma once

#include <vector>
#include <cstdint>
#include <filesystem>
#include <webgpu/webgpu.hpp>

class ResourceManager {
public:
	static bool loadGeometry(
		const std::filesystem::path& path,
		std::vector<float>& pointData,
		std::vector<uint16_t>& indexData
	);

	static wgpu::ShaderModule loadShaderModule(
		const std::filesystem::path& path,
		wgpu::Device device
	);

	// Loads a cubemap from a horizontal-cross PNG.
	// Fills facePixels with 6 consecutive faceSize×faceSize RGBA8 images in
	// WebGPU layer order: +X, -X, +Y, -Y, +Z, -Z.
	// Returns false and leaves facePixels unchanged on failure.
	static bool loadCubemapCross(
		const std::filesystem::path& path,
		std::vector<uint8_t>& facePixels,
		int& faceSize
	);
};