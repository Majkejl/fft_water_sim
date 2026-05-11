struct VertexInput {
	@location(0) position: vec3f,
	@location(1) uv: vec2f,
};

struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) fs_position: vec3f,
	@location(1) fs_normal: vec3f,
	@location(2) fs_uv: vec2f,
};

struct MyUniforms {
	model:      mat4x4<f32>,
	view:       mat4x4<f32>,
	proj:       mat4x4<f32>,
	eye:        vec3f,
	N:          f32,
	patch_size: f32,
	lambda:     f32,
}

@group(0) @binding(0) var<uniform> u:            MyUniforms;
@group(0) @binding(1) var          heightTexture: texture_2d<f32>;
@group(0) @binding(2) var          envSampler:    sampler;
@group(0) @binding(3) var          envMap:        texture_cube<f32>;
@group(0) @binding(4) var          slope_x_tex:   texture_2d<f32>;
@group(0) @binding(5) var          slope_y_tex:   texture_2d<f32>;
@group(0) @binding(6) var          disp_x_tex:    texture_2d<f32>;
@group(0) @binding(7) var          disp_y_tex:    texture_2d<f32>;
@group(0) @binding(8) var          foam_tex:      texture_2d<f32>;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;

	let uv       = in.uv;
	let N        = u.N;
	let inv      = 1.0 / (N * N);
	let scale_xy = 2.0 / u.patch_size;

	let h  = textureLoad(heightTexture, vec2i(uv * N), 0).r * inv;
	let dx = textureLoad(disp_x_tex,   vec2i(uv * N), 0).r * inv * scale_xy;
	let dy = textureLoad(disp_y_tex,   vec2i(uv * N), 0).r * inv * scale_xy;

	let base      = uv * 2.0 - 1.0;
	let localPos  = vec3f(base.x + u.lambda * dx, base.y + u.lambda * dy, h);
	let worldPos4 = u.model * vec4f(localPos, 1.0);

	out.fs_position = worldPos4.xyz;
	out.fs_normal   = vec3f(0.0);
	out.fs_uv       = uv;
	out.position    = u.proj * u.view * worldPos4;

	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {

	let inv = 1.0 / (u.N * u.N);
	let sx  = textureSample(slope_x_tex, envSampler, in.fs_uv).r * inv * (u.patch_size * 0.5);
	let sy  = textureSample(slope_y_tex, envSampler, in.fs_uv).r * inv * (u.patch_size * 0.5);
	let N   = normalize(vec3f(-sx, -sy, 1.0));

	let lightPos = vec3f(3.0, 4.0, 2.0);

	let L = normalize(lightPos - in.fs_position);
	let V = normalize(u.eye - in.fs_position);
	let H = normalize(L + V);

	let diff = max(dot(N, L), 0.0);
	let spec = pow(max(dot(N, H), 0.0), 64.0);

	let water = vec3f(0.0, 0.35, 0.75);

	let ambient  = 0.15 * water;
	let diffuse  = diff * water;
	let specular = spec * vec3f(1.0, 0.98, 0.9);

	let NdotV  = max(dot(N, V), 0.0);
	let fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

	let R        = reflect(-V, N);
	let envColor = textureSample(envMap, envSampler, vec3f(R.x, R.z, -R.y)).rgb;

	let lit        = ambient + diffuse + specular + fresnel * envColor;
	let foam       = textureSample(foam_tex, envSampler, in.fs_uv).r;
	let foam_color = vec3f(0.9, 0.95, 1.0);

	return vec4f(mix(lit, foam_color, foam * 0.85), 1.0);
}
