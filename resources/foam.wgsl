struct FoamUniforms {
    lambda:    f32,
    threshold: f32,
    erosion:   f32,
    fft_n:     f32,
    foam_add:  f32,
    _pad0:     f32,
    _pad1:     f32,
    _pad2:     f32,
}

@group(0) @binding(0) var<uniform> u:          FoamUniforms;
@group(0) @binding(1) var          foam_prev:  texture_2d<f32>;
@group(0) @binding(2) var          foam_out:   texture_storage_2d<r32float, write>;
@group(0) @binding(3) var          disp_x_tex: texture_2d<f32>;
@group(0) @binding(4) var          disp_y_tex: texture_2d<f32>;

@compute @workgroup_size(16, 16, 1)
fn computeFoam(@builtin(global_invocation_id) id: vec3<u32>) {
    let coord = vec2i(id.xy);
    let N     = i32(u.fft_n);
    let inv   = 1.0 / (u.fft_n * u.fft_n);

    /* Wrap-safe neighbour coordinates. */
    let xp = (coord + vec2i(1,   0)) % vec2i(N);
    let xm = (coord + vec2i(N-1, 0)) % vec2i(N);
    let yp = (coord + vec2i(0,   1)) % vec2i(N);
    let ym = (coord + vec2i(0, N-1)) % vec2i(N);

    /* Full 2×2 Jacobian determinant via finite differences. */
    let jxx = (textureLoad(disp_x_tex, xp, 0).r - textureLoad(disp_x_tex, xm, 0).r) * inv * 0.5;
    let jyy = (textureLoad(disp_y_tex, yp, 0).r - textureLoad(disp_y_tex, ym, 0).r) * inv * 0.5;
    let jxy = (textureLoad(disp_x_tex, yp, 0).r - textureLoad(disp_x_tex, ym, 0).r) * inv * 0.5;

    let J        = (1.0 + u.lambda * jxx) * (1.0 + u.lambda * jyy)
                 - (u.lambda * jxy) * (u.lambda * jxy);
    let biased_j = max(0.0, -(J - u.threshold));
    let new_f    = u.foam_add * biased_j;
    let eroded   = textureLoad(foam_prev, coord, 0).r * u.erosion;
    textureStore(foam_out, coord, vec4f(min(eroded + new_f, 1.0), 0.0, 0.0, 0.0));
}
