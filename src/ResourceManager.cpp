// In ResourceManager.cpp
#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

#ifdef _MSC_VER
#  pragma warning(push, 0)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

using namespace wgpu;

bool ResourceManager::loadGeometry(
	const std::filesystem::path& path,
	std::vector<float>& pointData,
	std::vector<uint16_t>& indexData
) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return false;
	}

	pointData.clear();
	indexData.clear();

	enum class Section {
		None,
		Points,
		Indices,
	};
	Section currentSection = Section::None;

	float value;
	uint16_t index;
	std::string line;
	while (!file.eof()) {
		getline(file, line);

		// overcome the `CRLF` problem
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line == "[points]") {
			currentSection = Section::Points;
		}
		else if (line == "[indices]") {
			currentSection = Section::Indices;
		}
		else if (line[0] == '#' || line.empty()) {
			// Do nothing, this is a comment
		}
		else if (currentSection == Section::Points) {
			std::istringstream iss(line);
			// Get x, y, r, g, b
			for (int i = 0; i < 5; ++i) {
				iss >> value;
				pointData.push_back(value);
			}
		}
		else if (currentSection == Section::Indices) {
			std::istringstream iss(line);
			// Get corners #0 #1 and #2
			for (int i = 0; i < 3; ++i) {
				iss >> index;
				indexData.push_back(index);
			}
		}
	}
	return true;
}

ShaderModule ResourceManager::loadShaderModule(const std::filesystem::path& path, Device device) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return nullptr;
	}
	file.seekg(0, std::ios::end);
	size_t size = file.tellg();
	std::string shaderSource(size, ' ');
	file.seekg(0);
	file.read(shaderSource.data(), size);

	ShaderModuleWGSLDescriptor shaderCodeDesc{};
	shaderCodeDesc.chain.next = nullptr;
	shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
	shaderCodeDesc.code = shaderSource.c_str();

	ShaderModuleDescriptor shaderDesc{};
#ifdef WEBGPU_BACKEND_WGPU
	shaderDesc.hintCount = 0;
	shaderDesc.hints = nullptr;
#endif
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
	return device.createShaderModule(shaderDesc);
}

bool ResourceManager::loadCubemapCross(
	const std::filesystem::path& path,
	std::vector<uint8_t>& facePixels,
	int& faceSize)
{
	int w, h, ch;
	stbi_uc* img = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
	if (!img) return false;

	// Horizontal cross layout: 4 columns × 3 rows, square image padded to w×w.
	//        [ +Y ]
	//  [-X] [+Z] [+X] [-Z]
	//        [ -Y ]
	faceSize = w / 4;
	const int yPad = (h - 3 * faceSize) / 2;

	// WebGPU cubemap layer order: +X(0), -X(1), +Y(2), -Y(3), +Z(4), -Z(5)
	// Standard Y-up cross assignment; Z-up→Y-up remapping is done in the shaders.
	const int faceX[6] = { 2*faceSize, 0,       faceSize,          faceSize,          faceSize,  3*faceSize };
	const int faceY[6] = { yPad+faceSize, yPad+faceSize, yPad, yPad+2*faceSize, yPad+faceSize, yPad+faceSize };

	const int bytesPerFace = faceSize * faceSize * 4;
	facePixels.resize(6 * bytesPerFace);

	for (int face = 0; face < 6; face++) {
		uint8_t* dst = facePixels.data() + face * bytesPerFace;
		for (int row = 0; row < faceSize; row++) {
			const stbi_uc* src = img + ((faceY[face] + row) * w + faceX[face]) * 4;
			std::memcpy(dst + row * faceSize * 4, src, static_cast<size_t>(faceSize) * 4);
		}
	}

	stbi_image_free(img);
	return true;
}