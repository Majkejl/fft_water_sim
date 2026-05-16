struct RenderUniforms {
	model: mat4x4<f32>,
	view: mat4x4<f32>,
	proj: mat4x4<f32>,
	eye: vec3f,
}

@group(0) @binding(0) var<uniform> u: RenderUniforms;
@group(0) @binding(2) var envSampler: sampler;
@group(0) @binding(3) var envMap: texture_cube<f32>;

struct SkyboxOutput {
	@builtin(position) position: vec4f,
	@location(0) direction: vec3f,
}

@vertex
fn vs_skybox(@builtin(vertex_index) vid: u32) -> SkyboxOutput {
	// Fullscreen triangle without array indexing (naga requires constant indices)
	let x = f32(vid & 1u) * 4.0 - 1.0;
	let y = f32((vid >> 1u) & 1u) * 4.0 - 1.0;
	let ndc = vec2f(x, y);

	let view_dir = vec3f(ndc.x / u.proj[0][0], ndc.y / u.proj[1][1], -1.0);
	let view3 = mat3x3f(u.view[0].xyz, u.view[1].xyz, u.view[2].xyz);
	let world_dir = view_dir * view3;

	var out: SkyboxOutput;
	out.position  = vec4f(ndc, 1.0, 1.0);
	out.direction = world_dir;
	return out;
}

@fragment
fn fs_skybox(in: SkyboxOutput) -> @location(0) vec4f {
	let d = normalize(in.direction);
	// Z-up world → Y-up cubemap: (x,y,z) → (x,z,-y)
	let cubemap_dir = vec3f(d.x, d.z, -d.y);
	return textureSample(envMap, envSampler, cubemap_dir);
}
