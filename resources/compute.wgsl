struct c_Uniforms
{
    time: f32,
}

@group(0) @binding(0) var<uniform> u: c_Uniforms;
@group(0) @binding(1) var heightTexture: texture_storage_2d<rgba8unorm, write>;


@compute @workgroup_size(32, 32, 1)
fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
    var height = 0.0;
    for (var i = 1; i <= 10; i++)
    {
        height += pow(0.5, f32(i + 2)) * (sin(f32(id.x + u32(i)) * 0.05 * f32(i) + u.time));
        height += pow(0.5, f32(i + 2)) * (sin(f32(id.y + u32(i)) * 0.05 * f32(i) + u.time));
    }
    textureStore(heightTexture, vec2i(id.xy), vec4f(height + 0.5));
}