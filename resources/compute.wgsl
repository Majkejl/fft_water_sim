@group(0) @binding(0) var heightTexture: texture_storage_2d<rgba8unorm, write>; 

@compute @workgroup_size(32, 32, 1)
fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
    var height = sin(f32(id.x) * 0.1) + 1.0;
    height += sin(f32(id.y) * 0.1) + 1.0;
    height /= 4.0;
    textureStore(heightTexture, vec2i(id.xy), vec4f(height));
}