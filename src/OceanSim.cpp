#include "OceanSim.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace wgpu;
using namespace pipeline_helpers;
using namespace texture_helpers;

// ---------------------------------------------------------------------------
// JONSWAP spectrum generation (CPU)
// ---------------------------------------------------------------------------

namespace {

double jonswap(double pos_x, double pos_y,
               double fetch, double wind_x, double wind_y,
               double enhancement = 3.3)
{
    using std::pow, std::exp, std::sqrt;

    double freq = sqrt(pos_x * pos_x + pos_y * pos_y);
    if (freq < 0.001) return 0.0;

    double wind  = sqrt(wind_x * wind_x + wind_y * wind_y);
    double g     = 9.81;
    double freq_p = 22.0 * pow(pow(g, 2.0) / (fetch * wind), 1.0 / 3.0);
    double alpha  = 0.076 * pow(pow(wind, 2.0) / (fetch * g), 0.22);
    double sigma  = (freq <= freq_p) ? 0.07 : 0.09;
    double r      = exp(-pow(freq - freq_p, 2.0) / (2.0 * pow(sigma * freq_p, 2.0)));

    double cos_theta = (pos_x * wind_x + pos_y * wind_y) / (freq * wind);
    double dir = std::max(0.0, cos_theta * cos_theta);

    return alpha * (pow(g, 2.0) / pow(freq, 5.0))
         * exp(-5.0 * pow(freq_p / freq, 4.0) / 4.0)
         * pow(enhancement, r)
         * dir * dir;
}

void generate_spectrum(const SimulationConfig& config,
                       std::vector<float>& spectrum, std::vector<float>& k_data)
{
    const int   N          = static_cast<int>(TEXTURE_SIZE);
    const float patch_size = config.ocean.patch_size;

    spectrum.assign(N * N * 4, 0.f);
    k_data.assign(N * N * 4, 0.f);

    std::mt19937 gen{ std::random_device{}() };
    std::normal_distribution<double> dist{ 0.0, 1.0 };

    for (int ky = 0; ky < N; ky++) {
        for (int kx = 0; kx < N; kx++) {
            int i  = kx + ky * N;
            int sx = (N - kx) % N;
            int sy = (N - ky) % N;
            int j  = sx + sy * N;

            int kx_m = (kx <= N / 2) ? kx : kx - N;
            int ky_m = (ky <= N / 2) ? ky : ky - N;

            float kx_phys = 2.f * static_cast<float>(std::numbers::pi) * kx_m / patch_size;
            float ky_phys = 2.f * static_cast<float>(std::numbers::pi) * ky_m / patch_size;
            float k_len   = std::sqrt(kx_phys * kx_phys + ky_phys * ky_phys);
            float omega   = std::sqrt(9.81f * k_len);

            k_data[4 * i + 0] = kx_phys;
            k_data[4 * i + 1] = ky_phys;
            k_data[4 * i + 2] = omega;
            k_data[4 * i + 3] = k_len;

            k_data[4 * j + 0] = -kx_phys;
            k_data[4 * j + 1] = -ky_phys;
            k_data[4 * j + 2] = omega;
            k_data[4 * j + 3] = k_len;

            double scale = std::sqrt(jonswap(kx_phys, ky_phys,
                                             config.ocean.fetch,
                                             config.ocean.wind_x,
                                             config.ocean.wind_y,
                                             config.ocean.enhancement) * 0.5)
                         * config.ocean.wave_amplitude;
            float re = static_cast<float>(dist(gen) * scale);
            float im = static_cast<float>(dist(gen) * scale);

            if (i == j) {
                spectrum[4 * i]     = re;
                spectrum[4 * i + 1] = 0.f;
            } else {
                spectrum[4 * i]     = re;
                spectrum[4 * i + 1] = im;
                spectrum[4 * j]     = re;
                spectrum[4 * j + 1] = -im;
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

OceanSim::~OceanSim()
{
    for (int i = 0; i < 2; i++) {
        height_texture_views[i].release();
        height_textures[i].destroy();
        height_textures[i].release();
        slope_x_texture_views[i].release();
        slope_x_textures[i].destroy();
        slope_x_textures[i].release();
        slope_y_texture_views[i].release();
        slope_y_textures[i].destroy();
        slope_y_textures[i].release();
        disp_x_texture_views[i].release();
        disp_x_textures[i].destroy();
        disp_x_textures[i].release();
        disp_y_texture_views[i].release();
        disp_y_textures[i].destroy();
        disp_y_textures[i].release();
        foam_texture_views[i].release();
        foam_textures[i].destroy();
        foam_textures[i].release();
        h_fft_bind_groups[i].release();
        sx_fft_bind_groups[i].release();
        sy_fft_bind_groups[i].release();
        dx_fft_bind_groups[i].release();
        dy_fft_bind_groups[i].release();
        foam_bind_groups[i].release();
    }

    time_spectrum_bind_group.release();

    spectrum_texture_view.release();
    spectrum_texture.destroy();
    spectrum_texture.release();

    butterfly_texture_view.release();
    butterfly_texture.destroy();
    butterfly_texture.release();

    k_data_texture_view.release();
    k_data_texture.destroy();
    k_data_texture.release();

    time_spectrum_bgl.release();
    time_spectrum_layout.release();
    fft_bgl.release();
    fft_layout.release();
    foam_bgl.release();
    foam_layout.release();

    compute_uniform_buffer.release();
    foam_uniform_buffer.release();

    time_spectrum_pipeline.release();
    fft_h_pipeline.release();
    fft_v_pipeline.release();
    foam_pipeline.release();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void OceanSim::init(wgpu::Device d, wgpu::Queue q, const SimulationConfig& config)
{
    device = d;
    queue  = q;

    init_pipelines();
    init_buffers();
    init_textures(config);
    init_bind_groups();
}

int OceanSim::tick(float time, const SimulationConfig& config)
{
    /* Pre-fill all TEXTURE_LOG uniform slots so each FFT stage dispatch can
       select its slot via a dynamic offset within a single compute pass. */
    std::vector<uint8_t> ubuf(compute_uniform_stride * TEXTURE_LOG, 0);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        ComputeUniforms cu{ time, static_cast<uint32_t>(s), TEXTURE_SIZE, TEXTURE_LOG };
        std::memcpy(ubuf.data() + s * compute_uniform_stride, &cu, sizeof(ComputeUniforms));
    }
    queue.writeBuffer(compute_uniform_buffer, 0, ubuf.data(), ubuf.size());

    FoamUniforms fu{
        config.ocean.lambda,
        config.foam.threshold,
        config.foam.erosion,
        static_cast<float>(TEXTURE_SIZE),
        config.foam.foam_add,
        0.f, 0.f, 0.f};
    queue.writeBuffer(foam_uniform_buffer, 0, &fu, sizeof(FoamUniforms));

    CommandEncoder encoder = device.createCommandEncoder(CommandEncoderDescriptor{});

    ComputePassDescriptor pass_desc;
    pass_desc.timestampWrites = nullptr;
    ComputePassEncoder pass   = encoder.beginComputePass(pass_desc);

    /* timeSpectrum: evolve h0(k) → h(k,t) and compute slope + displacement spectra. */
    pass.setPipeline(time_spectrum_pipeline);
    pass.setBindGroup(0, time_spectrum_bind_group, 0, nullptr);
    pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 16, 1);

    /* Horizontal IFFT for all 5 channels, TEXTURE_LOG stages each. */
    pass.setPipeline(fft_h_pipeline);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        uint32_t off = static_cast<uint32_t>(s) * compute_uniform_stride;
        pass.setBindGroup(0, h_fft_bind_groups [1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 16, TEXTURE_SIZE / 16, 1);
        pass.setBindGroup(0, sx_fft_bind_groups[1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 16, TEXTURE_SIZE / 16, 1);
        pass.setBindGroup(0, sy_fft_bind_groups[1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 16, TEXTURE_SIZE / 16, 1);
        pass.setBindGroup(0, dx_fft_bind_groups[1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 16, TEXTURE_SIZE / 16, 1);
        pass.setBindGroup(0, dy_fft_bind_groups[1 - s % 2], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 2 / 16, TEXTURE_SIZE / 16, 1);
    }

    /* Vertical IFFT — transposed dispatch, offset ping-pong index by TEXTURE_LOG. */
    pass.setPipeline(fft_v_pipeline);
    for (unsigned s = 0; s < TEXTURE_LOG; s++) {
        uint32_t off = static_cast<uint32_t>(s) * compute_uniform_stride;
        int bg = (TEXTURE_LOG + s + 1) % 2;
        pass.setBindGroup(0, h_fft_bind_groups [bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 2 / 16, 1);
        pass.setBindGroup(0, sx_fft_bind_groups[bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 2 / 16, 1);
        pass.setBindGroup(0, sy_fft_bind_groups[bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 2 / 16, 1);
        pass.setBindGroup(0, dx_fft_bind_groups[bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 2 / 16, 1);
        pass.setBindGroup(0, dy_fft_bind_groups[bg], 1, &off);
        pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 2 / 16, 1);
    }

    /* Foam: mark breaking pixels (J < threshold), erode previous accumulation. */
    pass.setPipeline(foam_pipeline);
    pass.setBindGroup(0, foam_bind_groups[foam_frame % 2], 0, nullptr);
    pass.dispatchWorkgroups(TEXTURE_SIZE / 16, TEXTURE_SIZE / 16, 1);

    pass.end();
#ifndef WEBGPU_BACKEND_WGPU
    wgpuComputePassEncoderRelease(pass);
#endif

    CommandBuffer commands = encoder.finish(CommandBufferDescriptor{});
    queue.submit(commands);
#ifndef WEBGPU_BACKEND_WGPU
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
#endif

    int foam_read_idx = 1 - static_cast<int>(foam_frame % 2);
    foam_frame++;
    return foam_read_idx;
}

void OceanSim::rebuild_spectrum(const SimulationConfig& config)
{
    upload_spectrum(config);
}

// ---------------------------------------------------------------------------
// Private: pipeline creation
// ---------------------------------------------------------------------------

void OceanSim::init_pipelines()
{
    // --- time_spectrum pipeline ---
    {
        ShaderModule ts_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/time_spectrum.wgsl", device);

        std::vector<BindGroupLayoutEntry> ts_entries = {
            uniform_layout        (0, ShaderStage::Compute, false, sizeof(ComputeUniforms)),
            storage_texture_layout(1, ShaderStage::Compute),
            texture_layout        (2, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            texture_layout        (3, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            storage_texture_layout(4, ShaderStage::Compute),
            storage_texture_layout(5, ShaderStage::Compute),
            storage_texture_layout(6, ShaderStage::Compute),
            storage_texture_layout(7, ShaderStage::Compute),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(ts_entries.size());
        bgl_desc.entries    = ts_entries.data();
        time_spectrum_bgl   = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&time_spectrum_bgl);
        time_spectrum_layout             = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = time_spectrum_layout;
        pipe_desc.compute.module        = ts_module;
        pipe_desc.compute.entryPoint    = "timeSpectrum";
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;
        time_spectrum_pipeline = device.createComputePipeline(pipe_desc);

        ts_module.release();
    }

    // --- FFT pipeline ---
    {
        ShaderModule fft_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/fft.wgsl", device);

        std::vector<BindGroupLayoutEntry> fft_entries = {
            uniform_layout        (0, ShaderStage::Compute, true, sizeof(ComputeUniforms)),
            storage_texture_layout(1, ShaderStage::Compute),
            texture_layout        (2, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            texture_layout        (3, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(fft_entries.size());
        bgl_desc.entries    = fft_entries.data();
        fft_bgl             = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&fft_bgl);
        fft_layout                       = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = fft_layout;
        pipe_desc.compute.module        = fft_module;
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;

        pipe_desc.compute.entryPoint = "fft_horizontal";
        fft_h_pipeline = device.createComputePipeline(pipe_desc);

        pipe_desc.compute.entryPoint = "fft_vertical";
        fft_v_pipeline = device.createComputePipeline(pipe_desc);

        fft_module.release();
    }

    // --- foam pipeline ---
    {
        ShaderModule foam_module = ResourceManager::load_shader_module(
            RESOURCE_DIR "/foam.wgsl", device);

        std::vector<BindGroupLayoutEntry> foam_entries = {
            uniform_layout        (0, ShaderStage::Compute, false, sizeof(FoamUniforms)),
            texture_layout        (1, ShaderStage::Compute, TextureSampleType::Float),
            storage_texture_layout(2, ShaderStage::Compute, TextureFormat::R32Float),
            texture_layout        (3, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
            texture_layout        (4, ShaderStage::Compute, TextureSampleType::UnfilterableFloat),
        };

        BindGroupLayoutDescriptor bgl_desc = {};
        bgl_desc.entryCount = static_cast<uint32_t>(foam_entries.size());
        bgl_desc.entries    = foam_entries.data();
        foam_bgl            = device.createBindGroupLayout(bgl_desc);

        PipelineLayoutDescriptor layout_desc = {};
        layout_desc.bindGroupLayoutCount = 1;
        layout_desc.bindGroupLayouts     = reinterpret_cast<WGPUBindGroupLayout*>(&foam_bgl);
        foam_layout                      = device.createPipelineLayout(layout_desc);

        ComputePipelineDescriptor pipe_desc;
        pipe_desc.layout                = foam_layout;
        pipe_desc.compute.module        = foam_module;
        pipe_desc.compute.entryPoint    = "computeFoam";
        pipe_desc.compute.constantCount = 0;
        pipe_desc.compute.constants     = nullptr;
        foam_pipeline = device.createComputePipeline(pipe_desc);

        foam_module.release();
    }
}

// ---------------------------------------------------------------------------
// Private: buffer creation
// ---------------------------------------------------------------------------

void OceanSim::init_buffers()
{
    compute_uniform_stride = (sizeof(ComputeUniforms) + 255u) & ~255u;

    BufferDescriptor buf_desc;
    buf_desc.mappedAtCreation = false;

    buf_desc.size  = compute_uniform_stride * TEXTURE_LOG;
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    compute_uniform_buffer = device.createBuffer(buf_desc);

    buf_desc.size  = sizeof(FoamUniforms);
    buf_desc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
    foam_uniform_buffer = device.createBuffer(buf_desc);
}

// ---------------------------------------------------------------------------
// Private: texture creation
// ---------------------------------------------------------------------------

void OceanSim::init_textures(const SimulationConfig& config)
{
    using wgpu::TextureUsage, wgpu::TextureFormat;

    const WGPUTextureUsageFlags ping_pong_usage = TextureUsage::TextureBinding | TextureUsage::StorageBinding;
    const WGPUTextureUsageFlags upload_usage    = TextureUsage::TextureBinding | TextureUsage::CopyDst;

    for (int i = 0; i < 2; i++) {
        height_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                    TextureFormat::RGBA32Float, ping_pong_usage);
        height_texture_views[i] = create_view_2d(height_textures[i], TextureFormat::RGBA32Float);
    }

    for (int i = 0; i < 2; i++) {
        slope_x_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                      TextureFormat::RGBA32Float, ping_pong_usage);
        slope_x_texture_views[i] = create_view_2d(slope_x_textures[i], TextureFormat::RGBA32Float);
        slope_y_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                      TextureFormat::RGBA32Float, ping_pong_usage);
        slope_y_texture_views[i] = create_view_2d(slope_y_textures[i], TextureFormat::RGBA32Float);
    }

    for (int i = 0; i < 2; i++) {
        disp_x_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                     TextureFormat::RGBA32Float, ping_pong_usage);
        disp_x_texture_views[i] = create_view_2d(disp_x_textures[i], TextureFormat::RGBA32Float);
        disp_y_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                     TextureFormat::RGBA32Float, ping_pong_usage);
        disp_y_texture_views[i] = create_view_2d(disp_y_textures[i], TextureFormat::RGBA32Float);
    }

    /* Foam textures need CopyDst for explicit zero-fill (D3D12 storage-only textures may not zero-init). */
    const WGPUTextureUsageFlags foam_usage =
        TextureUsage::TextureBinding | TextureUsage::StorageBinding | TextureUsage::CopyDst;
    {
        std::vector<float> zeros(TEXTURE_SIZE * TEXTURE_SIZE, 0.f);
        for (int i = 0; i < 2; i++) {
            foam_textures[i]      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                                       TextureFormat::R32Float, foam_usage);
            foam_texture_views[i] = create_view_2d(foam_textures[i], TextureFormat::R32Float);

            ImageCopyTexture dst = {};
            dst.texture  = foam_textures[i];
            dst.mipLevel = 0;
            dst.origin   = { 0, 0, 0 };
            dst.aspect   = TextureAspect::All;
            TextureDataLayout layout = {};
            layout.bytesPerRow  = TEXTURE_SIZE * sizeof(float);
            layout.rowsPerImage = TEXTURE_SIZE;
            Extent3D extent = { TEXTURE_SIZE, TEXTURE_SIZE, 1 };
            queue.writeTexture(dst, zeros.data(), zeros.size() * sizeof(float), layout, extent);
        }
    }

    spectrum_texture      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                              TextureFormat::RGBA32Float, upload_usage);
    spectrum_texture_view = create_view_2d(spectrum_texture, TextureFormat::RGBA32Float);

    k_data_texture      = create_texture_2d(device, TEXTURE_SIZE, TEXTURE_SIZE,
                                            TextureFormat::RGBA32Float, upload_usage);
    k_data_texture_view = create_view_2d(k_data_texture, TextureFormat::RGBA32Float);

    butterfly_texture      = create_texture_2d(device, TEXTURE_SIZE / 2, TEXTURE_LOG,
                                               TextureFormat::RGBA32Float, upload_usage);
    butterfly_texture_view = create_view_2d(butterfly_texture, TextureFormat::RGBA32Float);

    /* Precompute butterfly twiddle table: (N/2) × log2(N), RGBA32Float.
       Each texel (x, stage) = (tw.re, tw.im, a_idx, b_idx) for DIT IFFT. */
    {
        const float pi = static_cast<float>(std::numbers::pi);
        std::vector<float> bfly(TEXTURE_SIZE / 2 * TEXTURE_LOG * 4);
        for (unsigned s = 0; s < TEXTURE_LOG; s++) {
            int half_span = 1 << s;
            int span      = 2 * half_span;
            for (unsigned x = 0; x < TEXTURE_SIZE / 2; x++) {
                int local_j = x % half_span;
                int group   = x / half_span;
                int a_idx   = group * span + local_j;
                int b_idx   = a_idx + half_span;
                int k       = local_j * (TEXTURE_SIZE / span);
                float angle = +2.f * pi * k / TEXTURE_SIZE;
                int   idx   = (x + s * (TEXTURE_SIZE / 2)) * 4;
                bfly[idx + 0] = std::cos(angle);
                bfly[idx + 1] = std::sin(angle);
                bfly[idx + 2] = static_cast<float>(a_idx);
                bfly[idx + 3] = static_cast<float>(b_idx);
            }
        }
        ImageCopyTexture dst = {};
        dst.texture  = butterfly_texture;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, 0 };
        dst.aspect   = TextureAspect::All;
        TextureDataLayout layout = {};
        layout.bytesPerRow  = (TEXTURE_SIZE / 2) * 4 * sizeof(float);
        layout.rowsPerImage = TEXTURE_LOG;
        Extent3D extent = { static_cast<uint32_t>(TEXTURE_SIZE / 2), TEXTURE_LOG, 1 };
        queue.writeTexture(dst, bfly.data(), bfly.size() * sizeof(float), layout, extent);
    }

    upload_spectrum(config);
}

void OceanSim::upload_spectrum(const SimulationConfig& config)
{
    std::vector<float> spectrum, k_data;
    generate_spectrum(config, spectrum, k_data);

    auto upload = [&](Texture tex, const std::vector<float>& data) {
        ImageCopyTexture dst = {};
        dst.texture  = tex;
        dst.mipLevel = 0;
        dst.origin   = { 0, 0, 0 };
        dst.aspect   = TextureAspect::All;
        TextureDataLayout layout = {};
        layout.bytesPerRow  = TEXTURE_SIZE * 4 * sizeof(float);
        layout.rowsPerImage = TEXTURE_SIZE;
        Extent3D extent = { TEXTURE_SIZE, TEXTURE_SIZE, 1 };
        queue.writeTexture(dst, data.data(), data.size() * sizeof(float), layout, extent);
    };

    upload(spectrum_texture, spectrum);
    upload(k_data_texture,   k_data);
}

// ---------------------------------------------------------------------------
// Private: bind group creation
// ---------------------------------------------------------------------------

void OceanSim::init_bind_groups()
{
    // --- time_spectrum bind group ---
    {
        std::vector<BindGroupEntry> e(8, Default);
        e[0].binding = 0;  e[0].buffer      = compute_uniform_buffer;
                           e[0].offset       = 0;
                           e[0].size         = sizeof(ComputeUniforms);
        e[1].binding = 1;  e[1].textureView  = height_texture_views[0];
        e[2].binding = 2;  e[2].textureView  = spectrum_texture_view;
        e[3].binding = 3;  e[3].textureView  = k_data_texture_view;
        e[4].binding = 4;  e[4].textureView  = slope_x_texture_views[0];
        e[5].binding = 5;  e[5].textureView  = slope_y_texture_views[0];
        e[6].binding = 6;  e[6].textureView  = disp_x_texture_views[0];
        e[7].binding = 7;  e[7].textureView  = disp_y_texture_views[0];

        BindGroupDescriptor desc;
        desc.layout     = time_spectrum_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();
        time_spectrum_bind_group = device.createBindGroup(desc);
    }

    // --- FFT bind groups (5 channels, ping-pong pairs) ---
    {
        std::vector<BindGroupEntry> e(4, Default);
        e[0].binding = 0;  e[0].buffer     = compute_uniform_buffer;
                           e[0].offset      = 0;
                           e[0].size        = sizeof(ComputeUniforms);
        e[3].binding = 3;  e[3].textureView = butterfly_texture_view;

        BindGroupDescriptor desc;
        desc.layout     = fft_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();

        auto make_pair = [&](wgpu::TextureView* views, wgpu::BindGroup* groups) {
            e[1].binding = 1;  e[1].textureView = views[0];
            e[2].binding = 2;  e[2].textureView = views[1];
            groups[0] = device.createBindGroup(desc);

            e[1].textureView = views[1];
            e[2].textureView = views[0];
            groups[1] = device.createBindGroup(desc);
        };

        make_pair(height_texture_views,  h_fft_bind_groups);
        make_pair(slope_x_texture_views, sx_fft_bind_groups);
        make_pair(slope_y_texture_views, sy_fft_bind_groups);
        make_pair(disp_x_texture_views,  dx_fft_bind_groups);
        make_pair(disp_y_texture_views,  dy_fft_bind_groups);
    }

    // --- foam bind groups ([i]: reads foam[i], writes foam[1-i]) ---
    {
        std::vector<BindGroupEntry> e(5, Default);
        e[0].binding = 0;  e[0].buffer      = foam_uniform_buffer;
                           e[0].offset       = 0;
                           e[0].size         = sizeof(FoamUniforms);
        e[3].binding = 3;  e[3].textureView  = disp_x_texture_views[0];
        e[4].binding = 4;  e[4].textureView  = disp_y_texture_views[0];

        BindGroupDescriptor desc;
        desc.layout     = foam_bgl;
        desc.entryCount = static_cast<uint32_t>(e.size());
        desc.entries    = e.data();

        for (int i = 0; i < 2; i++) {
            e[1].binding = 1;  e[1].textureView = foam_texture_views[i];
            e[2].binding = 2;  e[2].textureView = foam_texture_views[1 - i];
            foam_bind_groups[i] = device.createBindGroup(desc);
        }
    }
}
