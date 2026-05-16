#include "Renderer.h"
#include "ResourceManager.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace wgpu;
using namespace pipeline_helpers;
using namespace texture_helpers;

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

Renderer::~Renderer()
{
    if (foam_detail_texture_view) foam_detail_texture_view.release();
    if (foam_detail_texture)      { foam_detail_texture.destroy(); foam_detail_texture.release(); }

    if (cubemap_texture_view) cubemap_texture_view.release();
    if (cubemap_texture)      { cubemap_texture.destroy(); cubemap_texture.release(); }

    bind_group.release();
    pipeline_layout.release();
    bind_group_layout.release();

    uniform_buffer.release();
    vertex_buffer.release();
    index_buffer.release();

    depth_texture_view.release();
    depth_texture.destroy();
    depth_texture.release();

    sampler.release();

    pipeline.release();
    skybox_pipeline.release();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Renderer::init(wgpu::Device d, wgpu::Queue q, wgpu::TextureFormat fmt,
                    uint32_t w, uint32_t h, const SimulationConfig& /*config*/)
{
    device         = d;
    queue          = q;
    surface_format = fmt;
    width          = w;
    height         = h;

    init_pipelines();
    init_geometry();
    init_depth();
    init_sampler();
    init_foam_detail();

    BufferDescriptor buf_desc;
    buf_desc.mappedAtCreation = false;
    buf_desc.size  = sizeof(RenderUniforms);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    uniform_buffer = device.createBuffer(buf_desc);
}

void Renderer::rebuild_bind_group(const OceanSim& ocean, int foam_idx)
{
    if (bind_group) bind_group.release();

    std::vector<BindGroupEntry> entries(10, Default);
    entries[0].binding = 0;  entries[0].buffer      = uniform_buffer;
                              entries[0].offset       = 0;
                              entries[0].size         = sizeof(RenderUniforms);
    entries[1].binding = 1;  entries[1].textureView  = ocean.height_view();
    entries[2].binding = 2;  entries[2].sampler      = sampler;
    entries[3].binding = 3;  entries[3].textureView  = cubemap_texture_view;
    entries[4].binding = 4;  entries[4].textureView  = ocean.slope_x_view();
    entries[5].binding = 5;  entries[5].textureView  = ocean.slope_y_view();
    entries[6].binding = 6;  entries[6].textureView  = ocean.disp_x_view();
    entries[7].binding = 7;  entries[7].textureView  = ocean.disp_y_view();
    entries[8].binding = 8;  entries[8].textureView  = ocean.foam_view(foam_idx);
    entries[9].binding = 9;  entries[9].textureView  = foam_detail_texture_view;

    BindGroupDescriptor desc;
    desc.layout     = bind_group_layout;
    desc.entryCount = static_cast<uint32_t>(entries.size());
    desc.entries    = entries.data();
    bind_group      = device.createBindGroup(desc);
}

void Renderer::draw(wgpu::RenderPassEncoder pass, const RenderUniforms& uniforms)
{
    queue.writeBuffer(uniform_buffer, 0, &uniforms, sizeof(RenderUniforms));

    pass.pushDebugGroup("Water Mesh");
    pass.setPipeline(pipeline);
    pass.setVertexBuffer(0, vertex_buffer, 0, vertex_buffer.getSize());
    pass.setIndexBuffer(index_buffer, IndexFormat::Uint32, 0, index_buffer.getSize());
    pass.setBindGroup(0, bind_group, 0, nullptr);
    pass.drawIndexed(index_count, 9, 0, 0, 0);
    pass.popDebugGroup();

    pass.pushDebugGroup("Skybox");
    pass.setPipeline(skybox_pipeline);
    pass.setBindGroup(0, bind_group, 0, nullptr);
    pass.draw(3, 1, 0, 0);
    pass.popDebugGroup();
}

void Renderer::init_cubemap(const SimulationConfig& config)
{
    namespace fs = std::filesystem;
    for (auto const& entry : fs::directory_iterator(fs::path(RESOURCE_DIR "/Cubemap"))) {
        if (entry.path().extension() == ".png")
            cubemap_paths.push_back(entry.path().string());
    }
    std::sort(cubemap_paths.begin(), cubemap_paths.end());

    if (!cubemap_paths.empty())
        load_cubemap_texture(config.app.cubemap_index);
    /* Bind group is built by the caller (Application) once OceanSim is also ready. */
}

void Renderer::load_cubemap(int index, const OceanSim& ocean, int foam_idx)
{
    load_cubemap_texture(index);
    rebuild_bind_group(ocean, foam_idx);
}

void Renderer::load_cubemap_texture(int index)
{
    if (index < 0 || index >= static_cast<int>(cubemap_paths.size())) return;
    cubemap_index = index;

    std::vector<uint8_t> face_pixels;
    int face_size = 0;
    if (!ResourceManager::load_cubemap_cross(cubemap_paths[index], face_pixels, face_size)) {
        std::cerr << "Failed to load cubemap: " << cubemap_paths[index] << '\n';
        return;
    }

    if (cubemap_texture_view) cubemap_texture_view.release();
    if (cubemap_texture)      { cubemap_texture.destroy(); cubemap_texture.release(); }

    TextureDescriptor tex_desc;
    tex_desc.dimension       = TextureDimension::_2D;
    tex_desc.size            = { static_cast<uint32_t>(face_size), static_cast<uint32_t>(face_size), 6 };
    tex_desc.mipLevelCount   = 1;
    tex_desc.sampleCount     = 1;
    tex_desc.format          = TextureFormat::RGBA8Unorm;
    tex_desc.usage           = TextureUsage::TextureBinding | TextureUsage::CopyDst;
    tex_desc.viewFormatCount = 0;
    tex_desc.viewFormats     = nullptr;
    cubemap_texture = device.createTexture(tex_desc);

    TextureViewDescriptor view_desc;
    view_desc.aspect          = TextureAspect::All;
    view_desc.baseArrayLayer  = 0;
    view_desc.arrayLayerCount = 6;
    view_desc.baseMipLevel    = 0;
    view_desc.mipLevelCount   = 1;
    view_desc.dimension       = TextureViewDimension::Cube;
    view_desc.format          = TextureFormat::RGBA8Unorm;
    cubemap_texture_view      = cubemap_texture.createView(view_desc);

    const int bytes_per_face = face_size * face_size * 4;
    for (int face = 0; face < 6; face++) {
        ImageCopyTexture dst = {};
        dst.texture  = cubemap_texture;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, static_cast<uint32_t>(face) };
        dst.aspect   = TextureAspect::All;
        TextureDataLayout layout = {};
        layout.bytesPerRow  = static_cast<uint32_t>(face_size) * 4;
        layout.rowsPerImage = static_cast<uint32_t>(face_size);
        Extent3D extent = { static_cast<uint32_t>(face_size), static_cast<uint32_t>(face_size), 1 };
        queue.writeTexture(dst, face_pixels.data() + face * bytes_per_face,
                           static_cast<size_t>(bytes_per_face), layout, extent);
    }
}

// ---------------------------------------------------------------------------
// Private: pipeline creation
// ---------------------------------------------------------------------------

void Renderer::init_pipelines()
{
    ShaderModule water_module  = ResourceManager::load_shader_module(RESOURCE_DIR "/water.wgsl",  device);
    ShaderModule skybox_module = ResourceManager::load_shader_module(RESOURCE_DIR "/skybox.wgsl", device);
    if (!water_module || !skybox_module) {
        std::cerr << "Could not load shaders!\n";
        std::exit(1);
    }

    // --- bind group layout (shared by both render pipelines) ---
    std::vector<BindGroupLayoutEntry> entries = {
        uniform_layout (0, ShaderStage::Vertex | ShaderStage::Fragment, false, sizeof(RenderUniforms)),
        texture_layout (1, ShaderStage::Vertex | ShaderStage::Fragment),
        sampler_layout (2, ShaderStage::Fragment),
        texture_layout (3, ShaderStage::Fragment, TextureSampleType::Float, TextureViewDimension::Cube),
        texture_layout (4, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (5, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (6, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (7, ShaderStage::Vertex,   TextureSampleType::UnfilterableFloat),
        texture_layout (8, ShaderStage::Fragment, TextureSampleType::Float),
        texture_layout (9, ShaderStage::Fragment, TextureSampleType::Float),
    };

    BindGroupLayoutDescriptor bgl_desc = {};
    bgl_desc.entryCount = static_cast<uint32_t>(entries.size());
    bgl_desc.entries    = entries.data();
    bind_group_layout   = device.createBindGroupLayout(bgl_desc);

    PipelineLayoutDescriptor layout_desc = {};
    layout_desc.bindGroupLayoutCount = 1;
    layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&bind_group_layout);
    pipeline_layout                  = device.createPipelineLayout(layout_desc);

    TextureFormat depth_format = TextureFormat::Depth24Plus;

    BlendState blend;
    blend.color = { BlendOperation::Add, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha };
    blend.alpha = { BlendOperation::Add, BlendFactor::Zero,     BlendFactor::One };

    ColorTargetState color_target;
    color_target.format    = surface_format;
    color_target.blend     = &blend;
    color_target.writeMask = ColorWriteMask::All;

    DepthStencilState depth_state = Default;
    depth_state.format            = depth_format;
    depth_state.depthCompare      = CompareFunction::Less;
    depth_state.depthWriteEnabled = true;
    depth_state.stencilReadMask   = 0;
    depth_state.stencilWriteMask  = 0;

    // --- water render pipeline ---
    std::vector<VertexAttribute> attribs(2);
    attribs[0].format         = VertexFormat::Float32x3;
    attribs[0].offset         = 0;
    attribs[0].shaderLocation = 0;
    attribs[1].format         = VertexFormat::Float32x2;
    attribs[1].offset         = 3 * sizeof(float);
    attribs[1].shaderLocation = 1;

    VertexBufferLayout vbl;
    vbl.attributeCount = static_cast<uint32_t>(attribs.size());
    vbl.attributes     = attribs.data();
    vbl.arrayStride    = 5 * sizeof(float);
    vbl.stepMode       = VertexStepMode::Vertex;

    FragmentState water_fragment;
    water_fragment.module        = water_module;
    water_fragment.entryPoint    = "fs_main";
    water_fragment.constantCount = 0;
    water_fragment.constants     = nullptr;
    water_fragment.targetCount   = 1;
    water_fragment.targets       = &color_target;

    RenderPipelineDescriptor water_desc;
    water_desc.vertex.module        = water_module;
    water_desc.vertex.entryPoint    = "vs_main";
    water_desc.vertex.bufferCount   = 1;
    water_desc.vertex.buffers       = &vbl;
    water_desc.vertex.constantCount = 0;
    water_desc.vertex.constants     = nullptr;
    water_desc.primitive.topology         = PrimitiveTopology::TriangleList;
    water_desc.primitive.stripIndexFormat = IndexFormat::Undefined;
    water_desc.primitive.frontFace        = FrontFace::CCW;
    water_desc.primitive.cullMode         = CullMode::None;
    water_desc.fragment                   = &water_fragment;
    water_desc.depthStencil               = &depth_state;
    water_desc.multisample.count          = 1;
    water_desc.multisample.mask           = ~0u;
    water_desc.multisample.alphaToCoverageEnabled = false;
    water_desc.layout                     = pipeline_layout;
    pipeline = device.createRenderPipeline(water_desc);

    // --- skybox render pipeline ---
    DepthStencilState skybox_depth = Default;
    skybox_depth.format            = depth_format;
    skybox_depth.depthCompare      = CompareFunction::LessEqual;
    skybox_depth.depthWriteEnabled = false;
    skybox_depth.stencilReadMask   = 0;
    skybox_depth.stencilWriteMask  = 0;

    FragmentState skybox_fragment;
    skybox_fragment.module        = skybox_module;
    skybox_fragment.entryPoint    = "fs_skybox";
    skybox_fragment.constantCount = 0;
    skybox_fragment.constants     = nullptr;
    skybox_fragment.targetCount   = 1;
    skybox_fragment.targets       = &color_target;

    RenderPipelineDescriptor skybox_desc;
    skybox_desc.vertex.module        = skybox_module;
    skybox_desc.vertex.entryPoint    = "vs_skybox";
    skybox_desc.vertex.bufferCount   = 0;
    skybox_desc.vertex.buffers       = nullptr;
    skybox_desc.vertex.constantCount = 0;
    skybox_desc.vertex.constants     = nullptr;
    skybox_desc.primitive.topology         = PrimitiveTopology::TriangleList;
    skybox_desc.primitive.stripIndexFormat = IndexFormat::Undefined;
    skybox_desc.primitive.frontFace        = FrontFace::CCW;
    skybox_desc.primitive.cullMode         = CullMode::None;
    skybox_desc.fragment                   = &skybox_fragment;
    skybox_desc.depthStencil               = &skybox_depth;
    skybox_desc.multisample.count          = 1;
    skybox_desc.multisample.mask           = ~0u;
    skybox_desc.multisample.alphaToCoverageEnabled = false;
    skybox_desc.layout                     = pipeline_layout;
    skybox_pipeline = device.createRenderPipeline(skybox_desc);

    water_module.release();
    skybox_module.release();
}

// ---------------------------------------------------------------------------
// Private: geometry
// ---------------------------------------------------------------------------

void Renderer::init_geometry()
{
    const int size = static_cast<int>(MESH_SIZE);
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(size * size * 5));
    indices.reserve(static_cast<size_t>((size - 1) * (size - 1) * 6));

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            vertices.push_back(static_cast<float>(j) / (size - 1));
            vertices.push_back(static_cast<float>(i) / (size - 1));
            vertices.push_back(0.f);
            vertices.push_back(static_cast<float>(j) / (size - 1));
            vertices.push_back(static_cast<float>(i) / (size - 1));

            if (i < size - 1 && j < size - 1) {
                indices.push_back(static_cast<uint32_t>(j + i * size));
                indices.push_back(static_cast<uint32_t>(j + i * size + size));
                indices.push_back(static_cast<uint32_t>(j + i * size + 1));
                indices.push_back(static_cast<uint32_t>(j + i * size + 1));
                indices.push_back(static_cast<uint32_t>(j + i * size + size));
                indices.push_back(static_cast<uint32_t>(j + i * size + size + 1));
            }
        }
    }

    index_count = static_cast<uint32_t>(indices.size());

    BufferDescriptor buf_desc;
    buf_desc.mappedAtCreation = false;

    buf_desc.size  = vertices.size() * sizeof(float);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
    vertex_buffer  = device.createBuffer(buf_desc);
    queue.writeBuffer(vertex_buffer, 0, vertices.data(), buf_desc.size);

    buf_desc.size  = indices.size() * sizeof(uint32_t);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Index;
    index_buffer   = device.createBuffer(buf_desc);
    queue.writeBuffer(index_buffer, 0, indices.data(), buf_desc.size);
}

// ---------------------------------------------------------------------------
// Private: depth buffer
// ---------------------------------------------------------------------------

void Renderer::init_depth()
{
    TextureFormat depth_format = TextureFormat::Depth24Plus;

    TextureDescriptor depth_tex_desc;
    depth_tex_desc.dimension       = TextureDimension::_2D;
    depth_tex_desc.format          = depth_format;
    depth_tex_desc.mipLevelCount   = 1;
    depth_tex_desc.sampleCount     = 1;
    depth_tex_desc.size            = { width, height, 1 };
    depth_tex_desc.usage           = TextureUsage::RenderAttachment;
    depth_tex_desc.viewFormatCount = 1;
    depth_tex_desc.viewFormats     = reinterpret_cast<WGPUTextureFormat*>(&depth_format);
    depth_texture                  = device.createTexture(depth_tex_desc);

    TextureViewDescriptor depth_view_desc;
    depth_view_desc.aspect          = TextureAspect::DepthOnly;
    depth_view_desc.baseArrayLayer  = 0;
    depth_view_desc.arrayLayerCount = 1;
    depth_view_desc.baseMipLevel    = 0;
    depth_view_desc.mipLevelCount   = 1;
    depth_view_desc.dimension       = TextureViewDimension::_2D;
    depth_view_desc.format          = depth_format;
    depth_texture_view              = depth_texture.createView(depth_view_desc);
}

// ---------------------------------------------------------------------------
// Private: foam detail texture
// ---------------------------------------------------------------------------

void Renderer::init_foam_detail()
{
    std::vector<uint8_t> pixels;
    int fw = 0, fh = 0;
    if (!ResourceManager::load_image(RESOURCE_DIR "/foam_detail.jpg", pixels, fw, fh)) {
        std::cerr << "Failed to load foam_detail.jpg\n";
        return;
    }

    TextureDescriptor td;
    td.dimension       = TextureDimension::_2D;
    td.format          = TextureFormat::RGBA8Unorm;
    td.size            = { static_cast<uint32_t>(fw), static_cast<uint32_t>(fh), 1 };
    td.mipLevelCount   = 1;
    td.sampleCount     = 1;
    td.usage           = TextureUsage::TextureBinding | TextureUsage::CopyDst;
    td.viewFormatCount = 0;
    td.viewFormats     = nullptr;
    foam_detail_texture      = device.createTexture(td);
    foam_detail_texture_view = create_view_2d(foam_detail_texture, TextureFormat::RGBA8Unorm);

    ImageCopyTexture dst = {};
    dst.texture  = foam_detail_texture;
    dst.mipLevel = 0;
    dst.origin   = { 0, 0, 0 };
    dst.aspect   = TextureAspect::All;
    TextureDataLayout layout = {};
    layout.bytesPerRow  = static_cast<uint32_t>(fw) * 4;
    layout.rowsPerImage = static_cast<uint32_t>(fh);
    Extent3D extent = { static_cast<uint32_t>(fw), static_cast<uint32_t>(fh), 1 };
    queue.writeTexture(dst, pixels.data(), pixels.size(), layout, extent);
}

// ---------------------------------------------------------------------------
// Private: sampler
// ---------------------------------------------------------------------------

void Renderer::init_sampler()
{
    SamplerDescriptor sampler_desc;
    sampler_desc.addressModeU  = AddressMode::Repeat;
    sampler_desc.addressModeV  = AddressMode::Repeat;
    sampler_desc.addressModeW  = AddressMode::Repeat;
    sampler_desc.magFilter     = FilterMode::Linear;
    sampler_desc.minFilter     = FilterMode::Linear;
    sampler_desc.mipmapFilter  = MipmapFilterMode::Linear;
    sampler_desc.lodMinClamp   = 0.f;
    sampler_desc.lodMaxClamp   = 1.f;
    sampler_desc.compare       = CompareFunction::Undefined;
    sampler_desc.maxAnisotropy = 1;
    sampler = device.createSampler(sampler_desc);
}
