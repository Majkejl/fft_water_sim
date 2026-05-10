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
	model: mat4x4<f32>,
	view: mat4x4<f32>,
	proj: mat4x4<f32>,
	eye: vec3f,
}

@group(0) @binding(0) var<uniform> u: MyUniforms;
@group(0) @binding(1) var heightTexture: texture_2d<f32>;
@group(0) @binding(2) var envSampler: sampler;
@group(0) @binding(3) var envMap: texture_cube<f32>;

fn sampleHeight(uv: vec2f) -> f32 {
	let N = 256.0;
	return textureLoad(heightTexture, vec2i(uv * N), 0).r / (N * N);
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;

	let uv = in.uv;

	let h = sampleHeight(uv);

	let eps = 1.0 / 256.0;

	let hL = sampleHeight(uv + vec2f(-eps, 0.0));
	let hR = sampleHeight(uv + vec2f( eps, 0.0));
	let hD = sampleHeight(uv + vec2f(0.0, -eps));
	let hU = sampleHeight(uv + vec2f(0.0,  eps));

	let dx = vec3f(2.0 * eps, 0.0, hR - hL);
	let dy = vec3f(0.0, 2.0 * eps, hU - hD);

	let normal = normalize(cross(dx, dy));

	let localPos = vec3f(uv * 2.0 - 1.0, h);

	let worldPos4 = u.model * vec4f(localPos, 1.0);

	out.fs_position = worldPos4.xyz;
	out.fs_normal = normalize((u.model * vec4f(normal, 0.0)).xyz);
	out.fs_uv = uv;

	out.position = u.proj * u.view * worldPos4;

	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {

	let N = normalize(in.fs_normal);

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

	let NdotV   = max(dot(N, V), 0.0);
	let fresnel  = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

	let R        = reflect(-V, N);
	// Z-up world → Y-up cubemap: (x,y,z) → (x,z,-y)
	let envColor = textureSample(envMap, envSampler, vec3f(R.x, R.z, -R.y)).rgb;

	return vec4f(ambient + diffuse + specular + fresnel * envColor, 1.0);
}
