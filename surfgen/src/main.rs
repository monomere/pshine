#![feature(slice_as_chunks)]

use core::{f32, f64};
use noise::utils::NoiseMapBuilder;

struct CraterNoise<_W: noise::NoiseFn<f64, 3>> {
	seed: u32,
	crater_noise_distance: _W,
	crater_noise_value: _W,
}

impl<_W: noise::NoiseFn<f64, 3>> noise::NoiseFn<f64, 3> for CraterNoise<_W> {
	fn get(&self, point: [f64; 3]) -> f64 {
		use vek::Vec3;
		type Vec3f32 = Vec3<f32>;
		type Vec3f64 = Vec3<f64>;

		let p: Vec3f32 = Vec3f64::from(point).as_();
		let v = self.crater_noise_value.get(point);
		let d = self.crater_noise_distance.get(point) * 0.5 + 0.5
			+ v * 0.2;
		let l = 0.3;
		if d < l && v < 0.0 {
			d / l  - 0.5
		} else {
			0.0
		}
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
	base: impl noise::NoiseFn<f64, 3>,
) -> impl noise::NoiseFn<f64, 3> {
	noise::Displace::new(base,
		make_noise_scale(
			0.0, 0.5, 0.2,
			noise::Perlin::new(seed.wrapping_mul(69))),
		make_noise_scale(
			0.0, 0.5, 0.2,
			noise::Perlin::new(seed.wrapping_mul(123))),
		make_noise_scale(
			0.0, 0.5, 0.2,
			noise::Perlin::new(seed.wrapping_mul(231))),
		make_noise_scale(
			0.0, 0.5, 0.2,
			noise::Perlin::new(seed.wrapping_mul(321))))
}

fn new_crater_noise(seed: u32) -> impl noise::NoiseFn<f64, 3> {
	use noise::core::worley::ReturnType;
	let base = noise::Worley::new(seed)
		.set_distance_function(
		noise::core::worley::distance_functions::euclidean)
		.set_frequency(5.0);
	let off = vek::Vec3::new(453.0, 1213.2, 922.0);
	CraterNoise {
		seed,
		crater_noise_distance:
			make_const_displacement(off,
				make_displacement(seed, base.clone()
					.set_return_type(ReturnType::Distance))),
		crater_noise_value:
			make_const_displacement(off,
				make_displacement(seed, base.clone()
					.set_return_type(ReturnType::Value))),
	}
}

fn main() {
	let final_noise =
	noise::Add::new(
		noise::ScaleBias::new(noise::ScalePoint::new(noise::Fbm::<noise::Perlin>
			::new(123123123))
		.set_scale(2.0)).set_scale(0.5).set_bias(0.5),
		new_crater_noise(69420 /* haha */)
		);

	let noise_map = noise::utils::SphereMapBuilder::new(final_noise)
		.set_bounds(-90.0, 90.0, -180.0, 180.0)
		.set_size(2048, 1024)
		.build();

	let noise_image = noise::utils::ImageRenderer::new()
		.set_gradient(noise::utils::ColorGradient::new()
			.add_gradient_point(-1.0, [82, 80, 79, 255])
			.add_gradient_point(0.5, [128, 121, 110, 255])
			.add_gradient_point(1.1, [198, 185, 180, 255]))
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
