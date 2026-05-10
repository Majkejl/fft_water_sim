struct c_Uniforms
{
    time: f32,
    stage: u32,
    N: u32,
}

@group(0) @binding(4) var<uniform> u: c_Uniforms;
@group(0) @binding(5) var outTexture: texture_storage_2d<rgba32float, write>;
@group(0) @binding(6) var inTexture: texture_2d<f32>;
@group(0) @binding(7) var spectrumTexture: texture_2d<f32>;
@group(0) @binding(8) var butterflyTex: texture_2d<f32>;
@group(0) @binding(9) var kDataTexture: texture_2d<f32>;


// @compute @workgroup_size(32, 32, 1)
// fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
//     var height = 0.0;
//     for (var i = 1; i <= 10; i++)
//     {
//         var amp = pow(0.5, f32(i + 2));
//         var pos = f32(id.x) * cos(f32(i)) + f32(id.y) * sin(f32(i));
//         var freq = 0.05 * f32(i);
//         var shift = u.time * (0.3 + f32(i * 11 % 7) / 3.5);
//         height += amp * sin( pos * freq + shift);
//     }
//     textureStore(outTexture, vec2i(id.xy), vec4f(height + 0.5));
// }

fn reverse(x: u32, log2n: u32) -> u32 {
    return reverseBits(x) >> (32u - log2n);
}

@compute @workgroup_size(32, 32, 1)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let coord = vec2<i32>(gid.xy);

    let value = textureLoad(spectrumTexture, coord, 0);

    textureStore(outTexture, coord, vec4(u.time / 100));
}

const G: f32 = 9.81;

fn complex_mul(a: vec2<f32>, b: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x
    );
}

fn complex_exp(theta: f32) -> vec2<f32> {
    return vec2<f32>(cos(theta), sin(theta));
}


@compute @workgroup_size(32, 32, 1)
fn timeSpectrum(@builtin(global_invocation_id) id : vec3<u32>) {
    let N = u.N;

    let coord = vec2<u32>(id.xy);

    // -----------------------------
    // 1. Load h0(k)
    // -----------------------------
    let h0 = textureLoad(spectrumTexture, coord, 0).rg;

    // -----------------------------
    // 2. Reconstruct k vector
    //    (assuming centered spectrum)
    // -----------------------------

    let kx = textureLoad(kDataTexture, coord, 0).r;
    let ky = textureLoad(kDataTexture, coord, 0).g;

    let k = length(vec2<f32>(kx, ky));

    // -----------------------------
    // 3. Dispersion relation
    // -----------------------------
    let omega = textureLoad(kDataTexture, coord, 0).b;

    let t = u.time;

    // -----------------------------
    // 4. Phase factors
    // -----------------------------
    let phase = omega * t;

    let c = cos(phase);
    let s = sin(phase);

    // e^{iwt}
    let exp_pos = vec2<f32>(c, s);

    // e^{-iwt}
    let exp_neg = vec2<f32>(c, -s);

    // -----------------------------
    // 5. h*(−k)
    // NOTE: approximate by symmetric lookup
    // -----------------------------
    let mirrored = vec2<u32>(
        (N - coord.x) % N,
        (N - coord.y) % N
    );

    let h0_conj = textureLoad(spectrumTexture, mirrored, 0).rg;
    let h0_neg = vec2<f32>(h0_conj.x, -h0_conj.y);

    // -----------------------------
    // 6. Final height in frequency domain
    // -----------------------------
    let term1 = complex_mul(h0, exp_pos);
    let term2 = complex_mul(h0_neg, exp_neg);

    let h = term1 + term2;

    // -----------------------------
    // 7. Output
    // -----------------------------
    textureStore(outTexture, coord, vec4<f32>(h, 0.0, 1.0));

}

// id.x = butterfly index 0..N/2-1, id.y = row 0..N-1
@compute @workgroup_size(32, 32, 1)
fn fft_horizontal(@builtin(global_invocation_id) id: vec3<u32>) {
    let stage  = u.stage;
    let data   = textureLoad(butterflyTex, vec2i(i32(id.x), i32(stage)), 0);
    let tw     = data.rg;
    let writ_a = i32(data.b + 0.5);
    let writ_b = i32(data.a + 0.5);

    let log2n  = 8u;
    let read_a = select(writ_a, i32(reverse(u32(writ_a), log2n)), stage == 0u);
    let read_b = select(writ_b, i32(reverse(u32(writ_b), log2n)), stage == 0u);

    let row = i32(id.y);
    let a   = textureLoad(inTexture, vec2i(read_a, row), 0).rg;
    let b   = textureLoad(inTexture, vec2i(read_b, row), 0).rg;

    let out_a = a + complex_mul(tw, b);
    let out_b = a - complex_mul(tw, b);

    textureStore(outTexture, vec2i(writ_a, row), vec4f(out_a, 0.0, 1.0));
    textureStore(outTexture, vec2i(writ_b, row), vec4f(out_b, 0.0, 1.0));
}

// id.x = col 0..N-1, id.y = butterfly index 0..N/2-1
@compute @workgroup_size(32, 32, 1)
fn fft_vertical(@builtin(global_invocation_id) id: vec3<u32>) {
    let stage  = u.stage;
    let data   = textureLoad(butterflyTex, vec2i(i32(id.y), i32(stage)), 0);
    let tw     = data.rg;
    let writ_a = i32(data.b + 0.5);
    let writ_b = i32(data.a + 0.5);

    let log2n  = 8u;
    let read_a = select(writ_a, i32(reverse(u32(writ_a), log2n)), stage == 0u);
    let read_b = select(writ_b, i32(reverse(u32(writ_b), log2n)), stage == 0u);

    let col = i32(id.x);
    let a   = textureLoad(inTexture, vec2i(col, read_a), 0).rg;
    let b   = textureLoad(inTexture, vec2i(col, read_b), 0).rg;

    let out_a = a + complex_mul(tw, b);
    let out_b = a - complex_mul(tw, b);

    textureStore(outTexture, vec2i(col, writ_a), vec4f(out_a, 0.0, 1.0));
    textureStore(outTexture, vec2i(col, writ_b), vec4f(out_b, 0.0, 1.0));
}