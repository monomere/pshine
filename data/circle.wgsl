
struct V2F {
	@location(0) uv: vec2<f32>,
}

@fragment
fn frag(in: V2F) -> @location(0) vec4<f32> {
	let uv = in.uv * 2.0 - 1.0;
	const s = 0.005;
	let d = 1.0 - s - length(uv);
  return vec4(smoothstep(-s, s, d));
}
