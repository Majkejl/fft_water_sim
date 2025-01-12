@group(0) @binding(1) var heightTexture: texture_storage_2d<rgba8unorm, write>; 

@compute @workgroup_size(32)
fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {
    textureStore(heightTexture, id.xy, vec4f(f32(id.x) * 256. / 300., f32(id.y) * 256. / 300., 0., 0.));
}