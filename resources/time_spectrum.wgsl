struct c_Uniforms {
    time:  f32,
    stage: u32,
    N:     u32,
    log2n: u32,
}

@group(0) @binding(0) var<uniform> u:           c_Uniforms;
@group(0) @binding(1) var          h_out:        texture_storage_2d<rgba32float, write>;
@group(0) @binding(2) var          spectrum_tex: texture_2d<f32>;
@group(0) @binding(3) var          k_data_tex:   texture_2d<f32>;
@group(0) @binding(4) var          slope_x_out:  texture_storage_2d<rgba32float, write>;
@group(0) @binding(5) var          slope_y_out:  texture_storage_2d<rgba32float, write>;
@group(0) @binding(6) var          disp_x_out:   texture_storage_2d<rgba32float, write>;
@group(0) @binding(7) var          disp_y_out:   texture_storage_2d<rgba32float, write>;
@group(0) @binding(8) var          fold_out:     texture_storage_2d<rgba32float, write>;

fn complex_mul(a: vec2f, b: vec2f) -> vec2f {
    return vec2f(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

@compute @workgroup_size(32, 32, 1)
fn timeSpectrum(@builtin(global_invocation_id) id: vec3<u32>) {
    let N     = u.N;
    let coord = vec2i(id.xy);

    let h0    = textureLoad(spectrum_tex, coord, 0).rg;
    let kdata = textureLoad(k_data_tex,   coord, 0);
    let kx    = kdata.r;
    let ky    = kdata.g;
    let omega = kdata.b;

    let phase   = omega * u.time;
    let c       = cos(phase);
    let s       = sin(phase);
    let exp_pos = vec2f(c,  s);
    let exp_neg = vec2f(c, -s);

    let Ni       = i32(N);
    let mirrored = vec2i((Ni - coord.x) % Ni, (Ni - coord.y) % Ni);
    let h0_conj  = textureLoad(spectrum_tex, mirrored, 0).rg;
    let h0_neg   = vec2f(h0_conj.x, -h0_conj.y);

    let h = complex_mul(h0, exp_pos) + complex_mul(h0_neg, exp_neg);

    /* Slope spectra: i*k*H → (-k·h.im, k·h.re) */
    let sx = vec2f(-kx * h.y, kx * h.x);
    let sy = vec2f(-ky * h.y, ky * h.x);

    /* Choppy displacement spectra: i*(k/|k|)*H */
    let k_len = length(vec2(kx,ky));
    let inv_k = kdata.a;
    let dx    = vec2f(-kx * inv_k * h.y,  kx * inv_k * h.x);
    let dy    = vec2f(-ky * inv_k * h.y,  ky * inv_k * h.x);

    /* Jacobian fold spectrum: ∂Dx/∂x + ∂Dy/∂y = -|k|*H */
    let fd    = vec2f(-k_len * h.x, -k_len * h.y);

    textureStore(h_out,       coord, vec4f(h,  0.0, 1.0));
    textureStore(slope_x_out, coord, vec4f(sx, 0.0, 1.0));
    textureStore(slope_y_out, coord, vec4f(sy, 0.0, 1.0));
    textureStore(disp_x_out,  coord, vec4f(dx, 0.0, 1.0));
    textureStore(disp_y_out,  coord, vec4f(dy, 0.0, 1.0));
    textureStore(fold_out,    coord, vec4f(fd, 0.0, 1.0));
}
