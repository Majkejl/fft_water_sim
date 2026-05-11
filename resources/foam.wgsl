struct FoamUniforms {
    lambda:    f32,
    threshold: f32,
    erosion:   f32,
    fft_n:     f32,
    foam_n:    f32,
    _pad0:     f32,
    _pad1:     f32,
    _pad2:     f32,
}

@group(0) @binding(0) var<uniform> u:            FoamUniforms;
@group(0) @binding(1) var          fold_tex:     texture_2d<f32>;
@group(0) @binding(2) var          foam_prev:    texture_2d<f32>;
@group(0) @binding(3) var          foam_out:     texture_storage_2d<r32float, write>;
@group(0) @binding(4) var          fold_sampler: sampler;

@compute @workgroup_size(8, 8, 1)
fn computeFoam(@builtin(global_invocation_id) id: vec3<u32>) {
    let coord    = vec2i(id.xy);
    let fold_uv  = (vec2f(id.xy) + 0.5) / u.foam_n;
    let inv      = 1.0 / (u.fft_n * u.fft_n);
    let fold     = textureSampleLevel(fold_tex, fold_sampler, fold_uv, 0.0).r * inv;
    let J        = 1.0 + u.lambda * fold;
    let prev     = textureLoad(foam_prev, coord, 0).r;
    let eroded   = max(prev - u.erosion, 0.0);
    let new_f    = select(0.0, 1.0, J < u.threshold);
    textureStore(foam_out, coord, vec4f(min(eroded + new_f, 1.0), 0.0, 0.0, 0.0));
}
