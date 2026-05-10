struct c_Uniforms {
    time:  f32,
    stage: u32,
    N:     u32,
}

@group(0) @binding(0) var<uniform> u:           c_Uniforms;
@group(0) @binding(1) var          h_out:        texture_storage_2d<rgba32float, write>;
@group(0) @binding(2) var          spectrum_tex: texture_2d<f32>;
@group(0) @binding(3) var          k_data_tex:   texture_2d<f32>;
@group(0) @binding(4) var          slope_x_out:  texture_storage_2d<rgba32float, write>;
@group(0) @binding(5) var          slope_y_out:  texture_storage_2d<rgba32float, write>;

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

    let mirrored = (vec2i(N) - coord) % vec2i(N);
    let h0_conj  = textureLoad(spectrum_tex, mirrored, 0).rg;
    let h0_neg   = vec2f(h0_conj.x, -h0_conj.y);

    let h = complex_mul(h0, exp_pos) + complex_mul(h0_neg, exp_neg);

    /* Slope spectra: multiply h by ik in the frequency domain.
       i * (re, im) = (-im, re), so: slope = (-k·h.im, k·h.re) */
    let sx = vec2f(-kx * h.y, kx * h.x);
    let sy = vec2f(-ky * h.y, ky * h.x);

    textureStore(h_out,       coord, vec4f(h,  0.0, 1.0));
    textureStore(slope_x_out, coord, vec4f(sx, 0.0, 1.0));
    textureStore(slope_y_out, coord, vec4f(sy, 0.0, 1.0));
}
