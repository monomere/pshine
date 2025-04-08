#![allow(mixed_script_confusables)]
#![feature(slice_as_chunks)]

use core::f64;
use noise::{utils::NoiseMapBuilder as _, MultiFractal};
use rand::SeedableRng as _;

#[allow(non_upper_case_globals)]
const π: f64 = f64::consts::PI;

type Vec2f64 = vek::Vec2<f64>;
type Vec3f64 = vek::Vec3<f64>;

struct Crater {
	pos: Vec3f64,
	radius: f64,
	floor_height: f64,
}

fn smin(a: f64, b: f64, k: f64) -> f64 {
  let h = (0.5 + 0.5*(a-b)/k).clamp(0.0, 1.0);
  return a * (1.0 - h) + b * h - k*h*(1.0-h);
}

impl Crater {
	fn get(&self, d: f64) -> f64 {
		let bowl = 4.0 * d * d - 1.0;
		let edge = (d - 1.0) * (d - 1.0);
		let floor = -0.5;
		smin(smin(bowl, edge, 0.5), floor, -0.3)
	}
}

struct Craters {
	craters: Vec<Crater>,
}

impl noise::NoiseFn<f64, 3> for Craters {
	fn get(&self, point: [f64; 3]) -> f64 {
		let p = Vec3f64::from(point);

		let mut r = 0.0;
		for crater in &self.craters {
			let d2 = crater.pos.distance_squared(p);
			if d2 <= crater.radius * crater.radius {
				let d = d2.sqrt() / crater.radius;
				r += crater.get(d);
			}
		}

		r
	}
}

fn make_noise_scale(
	val_bias: f64,
	val_scale: f64,
	pnt_scale: f64,
	base: impl noise::NoiseFn<f64, 3>,
) -> impl noise::NoiseFn<f64, 3> {
	noise::ScaleBias::new(
		noise::ScalePoint::new(base)
			.set_scale(pnt_scale))
		.set_scale(val_scale)
		.set_bias(val_bias)
}

fn make_const_displacement(
	off: vek::Vec3<f64>,
	base: impl noise::NoiseFn<f64, 3>,
) -> impl noise::NoiseFn<f64, 3> {
	noise::Displace::new(base,
		noise::Constant::new(off.x),
		noise::Constant::new(off.y),
		noise::Constant::new(off.z),
		noise::Constant::new(0.0))
}


fn make_displacement(
	seed: u32,
	strength: f64,
	freq: f64,
	base: impl noise::NoiseFn<f64, 3>,
) -> impl noise::NoiseFn<f64, 3> {
	noise::Displace::new(base,
		make_noise_scale(
			0.0, strength, freq,
			noise::Perlin::new(seed.wrapping_mul(69))),
		make_noise_scale(
			0.0, strength, freq,
			noise::Perlin::new(seed.wrapping_mul(123))),
		make_noise_scale(
			0.0, strength, freq,
			noise::Perlin::new(seed.wrapping_mul(231))),
		make_noise_scale(
			0.0, strength, freq,
			noise::Perlin::new(seed.wrapping_mul(321))))
}

fn lat_lon_to_xyz(lat: f64, lon: f64) -> Vec3f64 {
	let r = lat.cos();
	let x = r * lon.cos();
	let y = lat.sin();
	let z = r * lon.sin();

	Vec3f64 { x, y, z }
}


fn tom_bombardil(
	crater_count: usize,
	radius_range: std::ops::RangeInclusive<f64>,
	radius_bias: f64,
	rng: &mut impl rand::Rng,
) -> impl noise::NoiseFn<f64, 3> {
	let mut craters = vec![];
	craters.reserve(crater_count);
	for _ in 0..crater_count {
		let lat = rng.random_range(-π/2.0..π/2.0);
		let lon = rng.random_range(-π..π);
		let pos = lat_lon_to_xyz(lat, lon);
		craters.push(Crater {
			pos,
			radius: rng.random_range(radius_range.clone())
				.powf(radius_bias),
			floor_height: -0.1,
		});
	}
	Craters { craters }
}

trait Surfgen {
	fn noise() -> impl noise::NoiseFn<f64, 3>;
	fn gradient() -> noise::utils::ColorGradient;
}

struct KJ61;
impl Surfgen for KJ61 {
	fn noise() -> impl noise::NoiseFn<f64, 3> {
		let mut rng = rand_chacha::ChaCha12Rng::from_seed([37; 32]);
		noise::Add::new(
			make_displacement(123123123, 20.0, 0.01,
				noise::Exponent::new(
					make_noise_scale(0.5, 0.5, 2.0,
						noise::Fbm::<noise::Perlin>::new(123123123)))
					.set_exponent(3.0)),
			make_noise_scale(0.0, 0.5, 1.0, tom_bombardil(
				512, 0.05..=0.5, 3.2, &mut rng))
		)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [94, 80, 64, 255])
			.add_gradient_point(0.0, [138, 120, 96, 255])
			.add_gradient_point(1.0, [208, 185, 140, 255])
	}
}


struct KJ62;
impl Surfgen for KJ62 {
	fn noise() -> impl noise::NoiseFn<f64, 3> {
		let mut rng = rand_chacha::ChaCha12Rng::from_seed([87; 32]);
		noise::Add::new(
			noise::Add::new(
				make_displacement(54987, 20.0, 0.01,
					noise::Exponent::new(
						make_noise_scale(0.5, 0.5, 1.0,
							noise::Fbm::<noise::Perlin>::new(829481)
								.set_frequency(0.5)
								.set_lacunarity(1.0)
								.set_octaves(6)))
						.set_exponent(3.0)),
				make_noise_scale(0.0, 0.5, 1.0,
					make_displacement(482729, 10.0, 0.02,
						noise::RidgedMulti::<noise::OpenSimplex>::new(382721)
							.set_frequency(1.2)
							.set_lacunarity(1.29)
							.set_attenuation(0.54)))
			),
			make_noise_scale(0.0, 0.2, 1.0,
				tom_bombardil(256, 0.1..=0.7, 3.2, &mut rng))
		)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [102, 16, 4, 255])
			.add_gradient_point(0.0, [139, 50, 26, 255])
			.add_gradient_point(1.0, [212, 102, 51, 255])
	}
}
struct KJ621;
impl Surfgen for KJ621 {
	fn noise() -> impl noise::NoiseFn<f64, 3> {
		let mut rng = rand_chacha::ChaCha12Rng::from_seed([32; 32]);
		noise::Add::new(
			noise::Add::new(
				make_displacement(2, 20.0, 0.01,
					noise::Exponent::new(
						make_noise_scale(0.5, 0.5, 1.0,
							noise::Fbm::<noise::Perlin>::new(2232)
								.set_frequency(0.9)
								.set_lacunarity(1.8)
								.set_persistence(0.8)
								.set_octaves(10)))
						.set_exponent(2.0)),
				make_noise_scale(0.0, 0.5, 1.0,
					make_displacement(4312, 10.0, 0.02,
						noise::RidgedMulti::<noise::OpenSimplex>::new(5442)
							.set_frequency(1.0)
							.set_lacunarity(1.49)
							.set_persistence(0.5)
							.set_attenuation(0.64)))
			),
			make_noise_scale(0.0, 0.3, 1.0,
				make_displacement(382, 0.01, 50.0,
					tom_bombardil(1024, 0.05..=0.9, 5.2, &mut rng)))
		)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [82, 89, 82, 255])
			.add_gradient_point(0.0, [139, 146, 130, 255])
			.add_gradient_point(1.0, [212, 222, 212, 255])
	}
}

fn main() {
	type S = KJ621;
	let final_noise = S::noise();

	let noise_map = noise::utils::SphereMapBuilder::new(final_noise)
		.set_bounds(-90.0, 90.0, -180.0, 180.0)
		.set_size(2048, 1024)
		.build();

	let noise_image = noise::utils::ImageRenderer::new()
		.set_gradient(S::gradient())
		.render(&noise_map);
	
	let mut buf = image::RgbImage::new(
		noise_image.size().0 as _,
		noise_image.size().1 as _,
	);
	let out = std::fs::File::create("output.png").unwrap();
	let mut wr = std::io::BufWriter::new(out);
	for (i, v) in noise_image.iter().enumerate() {
		buf.as_flat_samples_mut().as_mut_slice()
			.as_chunks_mut::<3>().0[i] = [v[0], v[1], v[2]];
	}
	buf.write_to(&mut wr, image::ImageFormat::Png).unwrap();
}
