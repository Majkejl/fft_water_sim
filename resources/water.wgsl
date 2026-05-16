struct VertexInput {
	@location(0) position: vec3f,
	@location(1) uv: vec2f,
	@builtin(instance_index) instance: u32,
};

struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) fs_position: vec3f,
	@location(1) fs_normal: vec3f,
	@location(2) fs_uv: vec2f,
};

struct RenderUniforms {
	model:      mat4x4<f32>,
	view:       mat4x4<f32>,
	proj:       mat4x4<f32>,
	eye:        vec3f,
	N:          f32,
	patch_size: f32,
	lambda:     f32,
}

@group(0) @binding(0) var<uniform> u:            RenderUniforms;
@group(0) @binding(1) var          heightTexture: texture_2d<f32>;
@group(0) @binding(2) var          envSampler:    sampler;
@group(0) @binding(3) var          envMap:        texture_cube<f32>;
@group(0) @binding(4) var          slope_x_tex:   texture_2d<f32>;
@group(0) @binding(5) var          slope_y_tex:   texture_2d<f32>;
@group(0) @binding(6) var          disp_x_tex:    texture_2d<f32>;
@group(0) @binding(7) var          disp_y_tex:    texture_2d<f32>;
@group(0) @binding(8) var          foam_tex:        texture_2d<f32>;
@group(0) @binding(9) var          foam_detail_tex: texture_2d<f32>;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;

	let uv       = in.uv;
	let N        = u.N;
	let inv      = 1.0 / (N * N);
	let scale_xy = 2.0 / u.patch_size;

	let tc = vec2i(uv * N) % vec2i(i32(N));
	let h  = textureLoad(heightTexture, tc, 0).r * inv;
	let dx = textureLoad(disp_x_tex,   tc, 0).r * inv * scale_xy;
	let dy = textureLoad(disp_y_tex,   tc, 0).r * inv * scale_xy;

	let sx = textureLoad(slope_x_tex, tc, 0).r * inv * (u.patch_size * 0.5);
	let sy = textureLoad(slope_y_tex, tc, 0).r * inv * (u.patch_size * 0.5);

	let tile_x = f32(i32(in.instance) % 3 - 1);
	let tile_y = f32(i32(in.instance) / 3 - 1);

	let base      = uv * 2.0 - 1.0;
	let localPos  = vec3f(base.x + u.lambda * dx + tile_x * 2.0,
	                      base.y + u.lambda * dy + tile_y * 2.0, h);
	let worldPos4 = u.model * vec4f(localPos, 1.0);

	out.fs_position = worldPos4.xyz;
	out.fs_normal   = normalize(vec3f(-sx, -sy, 1.0));
	out.fs_uv       = uv;
	out.position    = u.proj * u.view * worldPos4;

	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {

	let N = normalize(in.fs_normal);

	let L = normalize(vec3f(0.5, 0.5, 1.0));
	let V = normalize(u.eye - in.fs_position);
	let H = normalize(L + V);

	let foam        = textureSample(foam_tex,        envSampler, in.fs_uv).r;
	let foam_detail = textureSample(foam_detail_tex, envSampler, in.fs_uv * 8.0).r;
	let foam_mask   = foam * foam_detail;

	let diff       = max(dot(N, L), 0.0);
	let spec_power = mix(64.0, 4.0, foam_mask);
	let spec       = pow(max(dot(N, H), 0.0), spec_power);

	let water = vec3f(0.0, 0.35, 0.75);

	let sun_color = vec3f(1.0, 0.92, 0.72);

	let ambient  = 0.15 * water;
	let diffuse  = diff * water * sun_color;
	let specular = spec * sun_color;

	let NdotV  = max(dot(N, V), 0.0);
	let fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

	let R        = reflect(-V, N);
	let envColor = textureSample(envMap, envSampler, vec3f(R.x, R.z, -R.y)).rgb;

	let lit        = ambient + diffuse + specular + fresnel * envColor;
	let foam_color = vec3f(0.9, 0.95, 1.0);

	return vec4f(mix(lit, foam_color, foam_mask * 0.85), 1.0);
}
