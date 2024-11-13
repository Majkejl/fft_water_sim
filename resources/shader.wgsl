
/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
// struct VertexInput {
// 	@location(0) position: vec2f,
// 	@location(1) color: vec3f,
// };

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
// struct VertexOutput {
// 	@builtin(position) position: vec4f,
// 	// The location here does not refer to a vertex attribute, it just means
// 	// that this field must be handled by the rasterizer.
// 	// (It can also refer to another field of another struct that would be used
// 	// as input to the fragment shader.)
// 	@location(0) color: vec3f,
// };

// The memory location of the uniform is given by a pair of a *bind group* and a *binding*
@group(0) @binding(0) var<uniform> uTime: f32;

@vertex
fn vs_main(@location(0) position: vec2f) -> @builtin(position) vec4f {
    let ratio = 640.0 / 480.0;

    // We now move the scene depending on the time!
    var offset = vec2f(-0.6875, -0.463);
    offset += 0.3 * vec2f(cos(uTime), sin(uTime));

    return vec4f(position.x + offset.x, (position.y + offset.y) * ratio, 0.0, 1.0);

}

@fragment
fn fs_main(@builtin(position) position: vec4f) -> @location(0) vec4f {
	return position; // use the interpolated color coming from the vertex shader
}
