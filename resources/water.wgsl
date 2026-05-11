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
}

@group(0) @binding(0) var<uniform> u:           MyUniforms;
@group(0) @binding(1) var          heightTexture: texture_2d<f32>;
@group(0) @binding(2) var          envSampler:   sampler;
@group(0) @binding(3) var          envMap:       texture_cube<f32>;
@group(0) @binding(4) var          slope_x_tex:  texture_2d<f32>;
@group(0) @binding(5) var          slope_y_tex:  texture_2d<f32>;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;

	let uv  = in.uv;
	let N   = u.N;
	let inv = 1.0 / (N * N);

	/* textureLoad is legal in vertex shaders (no implicit derivatives). */
	let h = textureLoad(heightTexture, vec2i(uv * N), 0).r * inv;

	let localPos  = vec3f(uv * 2.0 - 1.0, h);
	let worldPos4 = u.model * vec4f(localPos, 1.0);

	out.fs_position = worldPos4.xyz;
	out.fs_normal   = vec3f(0.0);   /* populated in fragment shader from slope textures */
	out.fs_uv       = uv;
	out.position    = u.proj * u.view * worldPos4;

	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {

	let inv     = 1.0 / (u.N * u.N);
	let sx      = textureSample(slope_x_tex, envSampler, in.fs_uv).r * inv * (u.patch_size * 0.5);
	let sy      = textureSample(slope_y_tex, envSampler, in.fs_uv).r * inv * (u.patch_size * 0.5);
	let N       = normalize(vec3f(-sx, -sy, 1.0));

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
	/* Z-up world → Y-up cubemap: (x,y,z) → (x,z,-y) */
	let envColor = textureSample(envMap, envSampler, vec3f(R.x, R.z, -R.y)).rgb;

	return vec4f(ambient + diffuse + specular + fresnel * envColor, 1.0);
}
