struct FourierUniforms {
    time:  f32,
    stage: u32,
    N:     u32,
    log2n: u32,
}

@group(0) @binding(0) var<uniform> u:            FourierUniforms;
@group(0) @binding(1) var          out_tex:       texture_storage_2d<rgba32float, write>;
@group(0) @binding(2) var          in_tex:        texture_2d<f32>;
@group(0) @binding(3) var          butterfly_tex: texture_2d<f32>;

fn reverse(x: u32, log2n: u32) -> u32 {
    return reverseBits(x) >> (32u - log2n);
}

fn complex_mul(a: vec2f, b: vec2f) -> vec2f {
    return vec2f(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

/* id.x = butterfly index 0..N/2-1, id.y = row 0..N-1 */
@compute @workgroup_size(16, 16, 1)
fn fft_horizontal(@builtin(global_invocation_id) id: vec3<u32>) {
    let stage  = u.stage;
    let data   = textureLoad(butterfly_tex, vec2i(i32(id.x), i32(stage)), 0);
    let tw     = data.rg;
    let writ_a = i32(data.b + 0.5);
    let writ_b = i32(data.a + 0.5);

    let log2n  = u.log2n;
    let read_a = select(writ_a, i32(reverse(u32(writ_a), log2n)), stage == 0u);
    let read_b = select(writ_b, i32(reverse(u32(writ_b), log2n)), stage == 0u);

    let row   = i32(id.y);
    let a     = textureLoad(in_tex, vec2i(read_a, row), 0).rg;
    let b     = textureLoad(in_tex, vec2i(read_b, row), 0).rg;
    let out_a = a + complex_mul(tw, b);
    let out_b = a - complex_mul(tw, b);

    textureStore(out_tex, vec2i(writ_a, row), vec4f(out_a, 0.0, 1.0));
    textureStore(out_tex, vec2i(writ_b, row), vec4f(out_b, 0.0, 1.0));
}

/* id.x = col 0..N-1, id.y = butterfly index 0..N/2-1 */
@compute @workgroup_size(16, 16, 1)
fn fft_vertical(@builtin(global_invocation_id) id: vec3<u32>) {
    let stage  = u.stage;
    let data   = textureLoad(butterfly_tex, vec2i(i32(id.y), i32(stage)), 0);
    let tw     = data.rg;
    let writ_a = i32(data.b + 0.5);
    let writ_b = i32(data.a + 0.5);

    let log2n  = u.log2n;
    let read_a = select(writ_a, i32(reverse(u32(writ_a), log2n)), stage == 0u);
    let read_b = select(writ_b, i32(reverse(u32(writ_b), log2n)), stage == 0u);

    let col   = i32(id.x);
    let a     = textureLoad(in_tex, vec2i(col, read_a), 0).rg;
    let b     = textureLoad(in_tex, vec2i(col, read_b), 0).rg;
    let out_a = a + complex_mul(tw, b);
    let out_b = a - complex_mul(tw, b);

    textureStore(out_tex, vec2i(col, writ_a), vec4f(out_a, 0.0, 1.0));
    textureStore(out_tex, vec2i(col, writ_b), vec4f(out_b, 0.0, 1.0));
}
