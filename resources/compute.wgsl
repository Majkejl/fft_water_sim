struct c_Uniforms
{
    time: f32,
    stage: u32,
    N: u32,
}

@group(0) @binding(0) var<uniform> u: c_Uniforms;
@group(0) @binding(1) var outTexture: texture_storage_2d<rgba32float, write>;
@group(0) @binding(3) var inTexture: texture_2d<f32>;
@group(0) @binding(4) var spectrumTexture: texture_2d<f32>;
@group(0) @binding(5) var<storage, read> w_buff: array<f32>;


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

fn w(x: u32) -> vec2f {
    return vec2f(w_buff[2 * x], w_buff[2 * x + 1]);
}

fn reverse(x: u32, log2n: u32) -> u32 {
    return reverseBits(x) >> (32u - log2n);
}

@compute @workgroup_size(32, 32, 1)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let coord = vec2<i32>(gid.xy);

    let value = textureLoad(spectrumTexture, coord, 0);

    textureStore(outTexture, coord, value);
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
    let halfN = f32(N) * 0.5;

    let kx = (f32(coord.x) - halfN);
    let ky = (f32(coord.y) - halfN);

    let k = length(vec2<f32>(kx, ky));

    // -----------------------------
    // 3. Dispersion relation
    // -----------------------------
    let omega = sqrt(G * k);

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

@compute @workgroup_size(32, 32, 1) // TODO: change size probably in every @compute
fn fft_horizontal(@builtin(global_invocation_id) id : vec3<u32>) {
    
    // let iid = vec3<i32>(id);
    // let data = textureLoad(PrecomputedData, vec2<i32>(params.Step, iid.x), 0);
	// let inputsIndices = vec2<i32>(data.ba);

    // let input0 = textureLoad(InputBuffer, vec2<i32>(inputsIndices.x, iid.y), 0);
    // let input1 = textureLoad(InputBuffer, vec2<i32>(inputsIndices.y, iid.y), 0);

    // textureStore(OutputBuffer, iid.xy, vec4<f32>(
    //     input0.xy + complexMult(vec2<f32>(data.r, -data.g), input1.xy), 0., 0.
    // ));
}

@compute @workgroup_size(32, 32, 1) // TODO: change size probably in every @compute
fn fft_vertical(@builtin(global_invocation_id) id : vec3<u32>) {
    let N = u.N;
    let stage = u.stage;

    // column-major traversal (swap roles)
    let i = id.y + id.x * N;

    let stride = 1u << stage;
    let halfSpan = stride;

    let group = (i / (2u * halfSpan)) * (2u * halfSpan);
    let pos   = i % (2u * halfSpan);

    let a_i = group + (pos % halfSpan);
    let b_i = a_i + halfSpan;

    // NOTE: swapped addressing (column FFT)
    let x_a = a_i / N;
    let y_a = a_i % N;

    let x_b = b_i / N;
    let y_b = b_i % N;

    let a = textureLoad(inTexture, vec2<u32>(x_a, y_a), 0).r;
    let b = textureLoad(inTexture, vec2<u32>(x_b, y_b), 0).r;

    // twiddle factor
    let tw = w((pos % halfSpan) * (N / (2u * halfSpan)));

    let b_tw = b * tw.x; // simplified real FFT form (same assumption as horizontal)

    let out_a = a + b_tw;
    let out_b = a - b_tw;

    textureStore(outTexture, vec2(x_a, y_a), vec4f(out_a));
    textureStore(outTexture, vec2(x_b, y_b), vec4f(out_b));
}