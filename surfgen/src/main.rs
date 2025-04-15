#![allow(mixed_script_confusables)]
#![feature(slice_as_chunks)]
#![allow(unused)]

use core::f64;
use noise::{utils::NoiseMapBuilder as _, MultiFractal};
use rand::SeedableRng as _;

#[allow(non_upper_case_globals)]
const π: f64 = f64::consts::PI;

type Vec2f64 = vek::Vec2<f64>;
type Vec3f64 = vek::Vec3<f64>;

fn xyz_to_lat_lon(p: Vec3f64) -> (f64, f64) {
	let lon = p.z.atan2(p.x);
	let lat = (p.x / lon.cos()).acos();
	(lon, lat)
}

fn lat_lon_to_xyz(lat: f64, lon: f64) -> Vec3f64 {
	let r = lat.cos();
	let x = r * lon.cos();
	let y = lat.sin();
	let z = r * lon.sin();

	Vec3f64 { x, y, z }
}

struct Crater {
	pos: Vec3f64,
	radius: f64,
	floor_height: f64,
	steepness: f64,
	k1: f64,
	k2: f64,
}

fn smin(a: f64, b: f64, k: f64) -> f64 {
  let h = (0.5 + 0.5*(a-b)/k).clamp(0.0, 1.0);
  return a * (1.0 - h) + b * h - k*h*(1.0-h);
}

impl Crater {
	fn get(&self, d: f64) -> f64 {
		let bowl = 4.0 * d.powf(self.steepness + 1.0) - 1.0;
		let edge = (d - 1.0) * (d - 1.0);
		let floor = self.floor_height;
		smin(smin(bowl, edge, self.k1), floor, self.k2)
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

fn make_noise_scale_xyz(
	val_bias: f64,
	val_scale: f64,
	pnt_scale: Vec3f64,
	base: impl noise::NoiseFn<f64, 3>,
) -> impl noise::NoiseFn<f64, 3> {
	noise::ScaleBias::new(
		noise::ScalePoint::new(base)
			.set_x_scale(pnt_scale.x)
			.set_y_scale(pnt_scale.y)
			.set_z_scale(pnt_scale.z))
		.set_scale(val_scale)
		.set_bias(val_bias)
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

struct BombardmentConfig {
	radius_range: std::ops::RangeInclusive<f64>,
	radius_bias: f64,
	lat_bias: f64,
	lat_clearance: f64,
	depth_range: std::ops::RangeInclusive<f64>,
	steepness_range: std::ops::RangeInclusive<f64>
}

impl Default for BombardmentConfig {
	fn default() -> Self {
		Self {
			radius_range: 0.02..=0.95,
			radius_bias: 2.3,
			lat_bias: 1.7,
			lat_clearance: 0.05, 
			depth_range: 0.05..=0.8,
			steepness_range: 0.9..=1.1,
		}
	}
}

fn tom_bombardil(
	crater_count: usize,
	config: &BombardmentConfig,
	rng: &mut impl rand::Rng,
) -> impl noise::NoiseFn<f64, 3> {
	let mut craters = vec![];
	craters.reserve(crater_count);
	for _ in 0..crater_count {
		let lat: f64 = rng.random_range(-1.0..1.0);
		let lat = lat.abs().powf(config.lat_bias) * lat.signum()
			* (1.0 - config.lat_clearance) * π/2.0;
		let lon = rng.random_range(-π..π);
		let pos = lat_lon_to_xyz(lat, lon);
		craters.push(Crater {
			pos,
			radius: rng.random_range(config.radius_range.clone())
				.powf(config.radius_bias),
			floor_height: -rng.random_range(config.depth_range.clone()),
			steepness: rng.random_range(config.steepness_range.clone()),
			k1: 0.5,
			k2: -0.3,
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
				512, &BombardmentConfig {
					radius_bias: 3.2,
					radius_range: 0.05..=0.5,
					..Default::default()
				}, &mut rng))
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
								.set_persistence(0.8)
								.set_octaves(6)))
						.set_exponent(3.0)),
				make_noise_scale(0.0, 0.5, 1.0,
					make_displacement(482729, 10.0, 0.02,
						noise::RidgedMulti::<noise::OpenSimplex>::new(382721)
							.set_frequency(1.2)
							.set_lacunarity(2.29)
							.set_persistence(0.5)
							.set_attenuation(0.54)))
			),
			make_noise_scale(0.0, 0.2, 1.0,
				tom_bombardil(256, &BombardmentConfig {
					radius_bias: 3.2,
					radius_range: 0.1..=0.7,
					..Default::default()
				}, &mut rng))
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
					make_displacement(382, 0.003, 50.0,
						tom_bombardil(1024, &BombardmentConfig {
							radius_bias: 5.2,
							radius_range: 0.01..=0.9,
							..Default::default()
						}, &mut rng)))),
			make_noise_scale(0.0, 1.0, 1.0,
				make_displacement(382, 0.003, 50.0,
					Craters {
						craters: vec![Crater {
							floor_height: -0.2,
							radius: 1.8,
							pos: lat_lon_to_xyz(0.0, -0.2) * 1.0,
							steepness: 1.0,
							k1: 0.8,
							k2: -0.9,
						}]
					}))
		)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [82, 89, 82, 255])
			.add_gradient_point(0.0, [139, 146, 130, 255])
			.add_gradient_point(1.0, [212, 222, 212, 255])
	}
}

struct KJ63;
impl Surfgen for KJ63 {
	fn noise() -> impl noise::NoiseFn<f64, 3> {
		let mut rng = rand_chacha::ChaCha12Rng::from_seed([87; 32]);
		noise::Add::new(
			make_noise_scale(0.02, 0.2, 1.0,
				make_displacement(342, 20.0, 0.01,
					noise::Exponent::new(
						make_noise_scale_xyz(0.5, 0.5, Vec3f64::new(1.0, 10.0, 1.0),
							noise::Fbm::<noise::Perlin>::new(82481)
								.set_frequency(0.5)
								.set_lacunarity(1.0)
								.set_persistence(0.8)
								.set_octaves(6)))
						.set_exponent(5.0))),
				make_noise_scale_xyz(0.0, 0.5, Vec3f64::new(1.0, 8.0, 1.0),
					make_displacement(43837, 10.0, 0.2,
						noise::BasicMulti::<noise::OpenSimplex>::new(4918)
							.set_frequency(1.2)
							.set_lacunarity(2.29)
							.set_persistence(0.2)))
			)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [  6,   4,  32, 255])
			.add_gradient_point( 0.0, [ 70,  26, 139, 255])
			.add_gradient_point( 5.0, [152,  31, 172, 255])
	}
}

struct KJ631;
impl Surfgen for KJ631 {
	fn noise() -> impl noise::NoiseFn<f64, 3> {
		let mut rng = rand_chacha::ChaCha12Rng::from_seed([32; 32]);
		noise::Add::new(
			noise::Add::new(
				noise::Add::new(
					make_displacement(2, 20.0, 0.01,
						noise::Exponent::new(
							make_noise_scale(0.5, 0.5, 1.0,
								noise::Fbm::<noise::Perlin>::new(95842)
									.set_frequency(1.5)
									.set_lacunarity(2.3)
									.set_persistence(0.9)
									.set_octaves(10)))
							.set_exponent(2.0)),
					make_noise_scale(0.0, 0.5, 1.0,
						make_displacement(4312, 10.0, 0.02,
							noise::RidgedMulti::<noise::OpenSimplex>::new(941683)
								.set_frequency(1.0)
								.set_lacunarity(1.49)
								.set_persistence(0.5)
								.set_attenuation(0.64)))
				),
				make_noise_scale(0.0, 0.3, 1.0,
					make_displacement(5938, 0.003, 50.0,
						tom_bombardil(1024, &BombardmentConfig {
							radius_bias: 5.2,
							radius_range: 0.2..=0.9,
							..Default::default()
						}, &mut rng)))),
			make_noise_scale(0.0, -0.2, 8.0,
				noise::RidgedMulti::<noise::OpenSimplex>::new(95834)
					.set_persistence(1.8))
		)
	}

	fn gradient() -> noise::utils::ColorGradient {
		noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [82, 29, 12, 255])
			.add_gradient_point(0.0, [100, 62, 50, 255])
			.add_gradient_point(1.0, [126, 126, 148, 255])
	}
}

fn main() {
	type S = KJ631;
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
	let out = std::fs::File::create(std::env::args().nth(1)
		.expect("expected an output path")).unwrap();
	let mut wr = std::io::BufWriter::new(out);
	for (i, v) in noise_image.iter().enumerate() {
		buf.as_flat_samples_mut().as_mut_slice()
			.as_chunks_mut::<3>().0[i] = [v[0], v[1], v[2]];
	}
	buf.write_to(&mut wr, image::ImageFormat::Png).unwrap();
}
