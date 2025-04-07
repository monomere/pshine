use core::f32;

trait Projection {
	type Output;
	fn samples(&self) -> impl Iterator<Item = (Self::Output, vek::Vec3<f32>)>;
}

struct Cylindrical {
	samples_y: i32,
	samples_x: i32,
}

impl Projection for Cylindrical {
	type Output = vek::Vec2<i32>;

	fn samples(&self) -> impl Iterator<Item = (Self::Output, vek::Vec3<f32>)> {
		let half_y = self.samples_y / 2;
		let y_coeff: f32 = f32::consts::FRAC_PI_2 / half_y as f32;
		let half_x = self.samples_x / 2;
		let x_coeff: f32 = f32::consts::PI / half_x as f32;
		(-half_y..half_y).flat_map(move |y| (-half_x..half_x).map(move |x| {
			let lat = y as f32 * y_coeff;
			let lon = x as f32 * x_coeff;
			let v = vek::Vec3::new(
				f32::cos(lat) * f32::cos(lon),
				f32::cos(lat) * f32::sin(lon),
				f32::sin(lat)
			);
			(vek::Vec2::new(x + half_x, y + half_y), v)
		}))
	}
}

fn gather_to_image<C, P>(
	out: &mut impl image::GenericImage<Pixel = P>,
	proj: &impl Projection<Output = vek::Vec2<C>>,
	conv: &impl Fn(f64) -> P,
	n: &impl noise::NoiseFn<f64, 3>,
) where C: num_traits::int::PrimInt + num_traits::AsPrimitive<u32> {
	for (latlon, p) in proj.samples() {
		out.put_pixel(latlon.x.as_(), latlon.y.as_(),
			conv(n.get(p.as_().into_tuple().into())));
	}
}

fn u8_of_unorm(v: f32) -> u8 {
	(v.clamp(0.0, 1.0) * 255.0) as u8
}

fn grayscale(v: u8) -> image::Rgb<u8> {
	image::Rgb([v, v, v])
}

fn vekrgb_f32_to_u8(v: vek::Rgb<f32>) -> image::Rgb<u8> {
	image::Rgb([
		u8_of_unorm(v.r),
		u8_of_unorm(v.g),
		u8_of_unorm(v.b),
	])
} 

fn color_palette(
	x: f32
) -> vek::Rgb<f32> {
	let _c = |r, g, b| vek::Rgb::<f32>::new(r, g, b);
	let begin = _c(0.10, 0.08, 0.04);
	let ranges = [
		(0.1, _c(0.21, 0.20, 0.11)),
		(0.3, _c(0.41, 0.35, 0.21)),
		(0.6, _c(0.61, 0.65, 0.51)),
		(0.8, _c(0.78, 0.82, 0.79)),
	];
	let end = _c(0.87, 0.88, 0.81);
	let part = ranges.partition_point(|v| v.0 <= x);
	let ((at, a), (bt, b)) = if part == 0 {
		((0.0, begin), ranges[0])
	} else if part == ranges.len() {
		(ranges[ranges.len() - 1], (1.0, end))
	} else {
		(ranges[part - 1], ranges[part])
	};
	vek::Vec3::lerp(a.into(), b.into(),
		(x - at) / (bt - at)).into()
}

struct CraterNoise {
	seed: u32,
}

trait IntoTuple<T> {
	type Output;
	fn into_tuple(self) -> Self::Output;
}

impl<T> IntoTuple<T> for [T; 3] {
	type Output = (T, T, T);

	fn into_tuple(self) -> Self::Output {
		let [a, b, c] = self;
		(a, b, c)
	}
}

impl noise::NoiseFn<f64, 3> for CraterNoise {
	fn get(&self, point: [f64; 3]) -> f64 {
		use vek::Vec3;
		type Vec3f32 = Vec3<f32>;
		type Vec3f64 = Vec3<f64>;

		let p: Vec3f32 = Vec3f64::from(point).as_();
		// TODO: voronoi noise, select crater center, generate crater
		//       with polar coordinates around the crater center.
		0.0
	}
}

impl CraterNoise {
	fn new(seed: u32) -> Self {
		Self { seed }
	}
}

fn main() {
	let cyl = Cylindrical {
		samples_x: 2048,
		samples_y: 1024,
	};
	let mut buf = image::ImageBuffer::new(cyl.samples_x as u32, cyl.samples_y as u32);
	gather_to_image(&mut buf, &cyl, &|v|
		vekrgb_f32_to_u8(color_palette(v as f32)),
		&noise::Add::new(
			noise::ScaleBias::new(noise::ScalePoint::new(noise::Fbm::<noise::Perlin>
				::new(123123123))
			.set_scale(2.0)).set_scale(0.5).set_bias(0.5),
			CraterNoise::new(69420 /* haha */)));
	
	let out = std::fs::File::create("output.png").unwrap();
	let mut wr = std::io::BufWriter::new(out);
	buf.write_to(&mut wr, image::ImageFormat::Png).unwrap();
}
