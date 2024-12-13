
// /**
//  * A structure with fields labeled with vertex attribute locations can be used
//  * as input to the entry point of a shader.
//  */
struct VertexInput {
	@location(0) position: vec3f,
	//@location(1) color: vec3f,
};

// /**
//  * A structure with fields labeled with builtins and locations can also be used
//  * as *output* of the vertex shader, which is also the input of the fragment
//  * shader.
//  */
struct VertexOutput {
	@builtin(position) position: vec4f,
	// The location here does not refer to a vertex attribute, it just means
	// that this field must be handled by the rasterizer.
	// (It can also refer to another field of another struct that would be used
	// as input to the fragment shader.)
	@location(0) fs_position: vec3f,
	@location(1) fs_normal: vec3f,
};

struct MyUniforms
{
	model: mat4x4<f32>,
	view: mat4x4<f32>,
	proj: mat4x4<f32>,
	eye: vec3f,
}

// The memory location of the uniform is given by a pair of a *bind group* and a *binding*
@group(0) @binding(0) var<uniform> u: MyUniforms;
@group(0) @binding(1) var heightTexture: texture_2d<f32>; 

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    out.fs_position = (u.model * vec4f(in.position.xyz, 1)).xyz;
	
	let tex_pos = vec2i((out.fs_position.xy + 1) * 50);
	let height = in.position.z + textureLoad(heightTexture, tex_pos, 0).r * 0.5;
	out.fs_position.z = height * 2;

    out.position = u.proj * u.view * u.model * vec4f(in.position.xy, height, 1);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	return vec4f(in.fs_position.zzz, 1.0);
	// let tex_pos = vec2i((in.fs_position.xy + 1) * 50);
	// return vec4f(textureLoad(heightTexture, tex_pos, 0).rrr, 1.0);
}
