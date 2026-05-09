struct c_Uniforms
{
    time: f32,
}

@group(0) @binding(0) var<uniform> u: c_Uniforms;
@group(0) @binding(1) var outTexture: texture_storage_2d<rgba8unorm, write>;
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
//     textureStore(heightTexture, vec2i(id.xy), vec4f(height + 0.5));
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

@compute @workgroup_size(1024, 1, 1)
fn precompute(@builtin(global_invocation_id) id : vec3<u32>) {

}

@compute @workgroup_size(1024, 1, 1) // TODO: change size probably in every @compute
fn fft_horizontal(@builtin(global_invocation_id) id : vec3<u32>) {

}

@compute @workgroup_size(1024, 1, 1) // TODO: change size probably in every @compute
fn fft_vertical(@builtin(global_invocation_id) id : vec3<u32>) {

}