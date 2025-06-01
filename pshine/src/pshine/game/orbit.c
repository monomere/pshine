#include "game.h"

// Note: the math in this file is best viewed
//       with the Julia Mono font, or with UnifontEx.
//       Other fonts just don't support the unicode used,
//       or dont have it as a monospaced character, so
//       the alignment becomes wonky :(

void create_orbit_points(
	struct pshine_celestial_body *body,
	size_t count
) {
	body->orbit.cached_point_count = count;
	body->orbit.cached_points_own = calloc(count, sizeof(pshine_point3d_scaled));

	// Copy the orbit info because the true anomaly
	// and time are changed by the propagator.
	struct pshine_orbit_info o2 = body->orbit;

	double e = body->orbit.eccentricity;
	double a = body->orbit.semimajor;
	double μ = body->parent_ref->gravitational_parameter;
	double p = a * (1 - e*e);

	double u = NAN; // Mean motion.
	if (fabs(e - 1.0) < 1e-6) { // parabolic
		u = 2.0 * sqrt(μ / (p*p*p));
	} else if (e < 1.0) { // elliptic
		u = sqrt(μ / (a*a*a));
	} else if (e < 1.0) { // hyperbolic
		u = sqrt(μ / -(a*a*a));
	} else {
		unreachable();
	}

	double T = 2 * π / u; // Orbital period.
	double dt = T / (double)body->orbit.cached_point_count;

	for (size_t i = 0; i < body->orbit.cached_point_count; ++i) {
		propagate_orbit(dt, μ, &o2);
		double3 pos = double3mul(kepler_orbit_to_state_vector(&o2), PSHINE_SCS_FACTOR);
		*(double3*)&body->orbit.cached_points_own[i] = pos;
	}
}

void propagate_orbit(
	double delta_time,
	double gravitational_parameter,
	struct pshine_orbit_info *orbit
) {
	// https://orbital-mechanics.space/time-since-periapsis-and-keplers-equation/time-since-periapsis.html
	// https://orbital-mechanics.space/time-since-periapsis-and-keplers-equation/universal-variables.html
	// Also stuff stolen from https://git.sr.ht/~thepuzzlemaker/KerbalToolkit/tree/the-big-port/item/lib/src/kepler/orbits.rs
	// Thanks Wren :o)

	// We need to change the orbit's true anomaly (ν) based on the other parameters and the elapsed time.
	// The equations for the anomaly are different for different types of orbit, so we use a so-called
	// Universal anomaly here.
	//
	// The relation of the universal anomaly χ to the other anomalies:
	//
	//            ⎧
	//            ⎪ [TODO(tanν / 2)]        for parabolas, e = 1
	//            ⎪  ___
	//        χ = ⎨ √ a   E                 for ellipses, e < 1
	//            ⎪  ____
	//            ⎪ √ -a  F                 for hyperbolas, e > 1
	//            ⎩
	//
	// Let's define the Stumpff functions, useful in the Kepler equation:
	//                         _
	//              ⎧  1 - cos√z
	//              ⎪ ──────────╴,    if z > 0
	//              ⎪     z
	//              ⎪       __
	//              ⎪  cosh√-z - 1
	//       C(z) = ⎨ ────────────╴,  if z < 0
	//              ⎪      -z
	//              ⎪
	//              ⎪ 1
	//              ⎪ ─,              if z = 0.
	//              ⎩ 2
	//                  _       _
	//              ⎧  √z - sin√z
	//              ⎪ ────────────╴,    if z > 0
	//              ⎪     (√z)³
	//              ⎪       __    __
	//              ⎪  sinh√-z - √-z
	//       S(z) = ⎨ ──────────────╴,  if z < 0
	//              ⎪      (√-z)³
	//              ⎪
	//              ⎪ 1
	//              ⎪ ─,                if z = 0.
	//              ⎩ 6
	//
	// With some complicated maths, we can write the Kepler equation in terms of
	// the universal anomaly (χ):
	//
	//         r₀vᵣ₀
	//         ────╴ χ² C(αχ²) + (1 - αr₀)χ³ S(αχ²) + r₀χ = (t - t₀)√μ
	//          √μ
	//
	// Where   α = a⁻¹, t₀ is the initial time, r₀ is the initial position,
	//         vᵣ₀ is the initial projection of the velocity on the position vector,
	//         C(x) and S(x) are the Stumpff functions, defined above.
	//
	// We don't have the values for r₀ and v₀, but we know that at the apses
	// v ⟂ r => vᵣ₀ = 0. At the periapsis, r₀ is the distance from the vertex
	// to the focus, or semimajor axis minus the focal distance:
	//
	//        r₀ = a - ea = a(1 - e)
	//
	// Then, using t₀ = 0 at the periapsis, the equation becomes:
	//
	//        e χ³ S(χ²/a) + a(1 - e)χ - t√μ = 0 = f(χ)
	//
	// Unfrogtunately, this equation cannot be solved algebraically,
	// (since it is the Kepler equation [M = E - esinE], but reworded a bit),
	// so we need to use for example Newton's Method to find the roots.
	// Turns out, Laguerre algorithm is a bit better for this problem,
	// so we'll use it instead.
	//
	// For these algorithms, we need the derivative of our function:
	//
	//         df(χ)
	//        ──────╴ = e χ² C(χ²/a) + a(1 - e)
	//          dχ
	//
	// Once we find a good enough χ, we can figure out the anomalies that
	// we need, and change our orbit.

	double Δt = delta_time; // Change in time.
	double μ = gravitational_parameter;
	double a = orbit->semimajor; // The semimajor axis.
	double e = orbit->eccentricity; // The eccentricity.

	// Solve for χ using Newton's Method:
	orbit->time += Δt; // TODO: figure out t from the orbital params.
	// orbit->time = fmod(orbit->time, T);
	double tsqrtμ = orbit->time * sqrt(μ);
	double χ = tsqrtμ/fabs(a);
	double sqrtp = sqrt(a*(1.0 - e*e));
	{
		for (int i = 0; i < 50; ++i) {
			double αχ2 = χ*χ/a;
			double sqrtαχ2 = sqrt(fabs(αχ2));
			double Cαχ2 = NAN;
			{ // Stumpff's C(αχ²)
				if (fabs(αχ2) < 1e-6) Cαχ2 = 0.5;
				else if (αχ2 > 0.0) Cαχ2 = (1 - cos(sqrtαχ2)) / αχ2;
				else Cαχ2 = (cosh(sqrtαχ2) - 1) / -αχ2;
			}
			double Sαχ2 = NAN;
			{ // Stumpff's S(αχ²)
				if (fabs(αχ2) < 1e-6) Sαχ2 = 1./6.;
				else if (αχ2 > 0.0) Sαχ2 = (sqrtαχ2 - sin(sqrtαχ2)) / pow(sqrtαχ2, 3.0);
				else Sαχ2 = (sinh(sqrtαχ2) - sqrtαχ2) / pow(sqrtαχ2, 3.0);
			}
			double f = e * χ*χ*χ * Sαχ2 + a*(1-e) * χ - tsqrtμ;
			if (fabs(f) < 1e-3) break;
			double dfdχ = e * χ*χ * Cαχ2 + a*(1-e);
			χ -= f/dfdχ;
		}
	}

	// TODO: Solving for χ using the Laguerre algorithm, which is supposedly better
	// {
	// 	double n = 5;
	// 	double χᵢ = 0.0;
	// 	double αχᵢ² = χᵢ*χᵢ / a;
	// 	double t = .0, t₀ = .0;
	// }

	// PSHINE_DEBUG("chi = %f", χ);

	if (fabs(e - 1) < 1e-6) { // parabolic
		orbit->true_anomaly = 2 * atan(χ/sqrtp);
	} else if (e < 1) {
		double E = χ/sqrt(a);
		orbit->true_anomaly = 2 * atan(tan(E/2)*sqrt((1+e)/(1-e)));
	} else if (e > 1) {
		double F = χ/sqrt(-a);
		orbit->true_anomaly = 2 * atan(tanh(F/2)*sqrt((e+1)/(e-1)));
	}
}

// TODO: put parent_ref in pshine_orbit_info.

/// returns only the position for now.
double3 kepler_orbit_to_state_vector(
	const struct pshine_orbit_info *orbit
) {
	// Thank god https://orbital-mechanics.space exists!
	// The conversion formulas are taken from
	//   /classical-orbital-elements/orbital-elements-and-the-state-vector.html#orbital-elements-state-vector
	// But for some reason we get the semimajor axis equation from here instead, which includes the angular momentum (that we need):
	//   /time-since-periapsis-and-keplers-equation/universal-variables.html#orbit-independent-solution-the-universal-anomaly

	// TODO: rewrite this to make more sense.
	// Here's the semimajor axis equation:
	//
	//             h²     1
	//        a = --- ----------.
	//             μ    1 - e²
	//
	// We could extract just h², but we actually need the h²/μ term, so:
	//
	//             h²
	//        p = --- = a(1 - e²).
	//             μ
	//
	// First, we get the position in the perifocal frame of reference (relative to the orbit basically):
	//
	//             ⎛ cos ν ⎞       p         ⎛ cos ν ⎞   a(1 - e²)
	//        rₚ = ⎜   0   ⎟ ------------- = ⎜   0   ⎟ -------------.
	//             ⎝ sin ν ⎠  1 + e cos ν    ⎝ sin ν ⎠  1 + e cos ν
	//
	// Then we transform the perifocal frame to the "global" frame, rotating along each axis with these matrices:
	//
	//             ⎛ cos -ω  -sin -ω  0 ⎞
	//        R₁ = ⎜ sin -ω   cos -ω  0 ⎟,
	//             ⎝   0        0     1 ⎠
	//
	//             ⎛ 1    0        0    ⎞
	//        R₂ = ⎜ 0  cos -i  -sin -i ⎟,
	//             ⎝ 0  sin -i   cos -i ⎠
	//
	//             ⎛ cos -Ω  -sin -Ω  0 ⎞
	//        R₃ = ⎜ sin -Ω   cos -Ω  0 ⎟;
	//             ⎝   0        0     1 ⎠
	//
	// Now we can finally get the global position:
	//
	//        r = Rrₚ, where R = R₁R₂R₃.
	//

	// Some variables to correspond with the math notation:
	double ν = orbit->true_anomaly;
	double e = orbit->eccentricity;
	double a = orbit->semimajor;
	double Ω = orbit->longitude;
	double i = orbit->inclination;
	double ω = orbit->argument;

	double3 rₚ = double3mul(double3xyz(cos(ν), 0.0, sin(ν)),
		1'000'000 * a * (1 - e*e) / (1 + e * cos(ν)));

	double3x3 R1;
	R1.v3s[0] = double3xyz( cos(-ω), 0.0, sin(-ω));
	R1.v3s[1] = double3xyz(     0.0, 1.0,     0.0);
	R1.v3s[2] = double3xyz(-sin(-ω), 0.0, cos(-ω));
	double3x3 R2;
	R2.v3s[0] = double3xyz(1.0,     0.0,      0.0);
	R2.v3s[1] = double3xyz(0.0, cos(-i), -sin(-i));
	R2.v3s[2] = double3xyz(0.0, sin(-i),  cos(-i));
	double3x3 R3;
	R3.v3s[0] = double3xyz( cos(-Ω), 0.0, sin(-Ω));
	R3.v3s[1] = double3xyz(     0.0, 1.0,     0.0);
	R3.v3s[2] = double3xyz(-sin(-Ω), 0.0, cos(-Ω));

	double3x3 R = R2;
	double3x3mul(&R, &R1);
	double3x3mul(&R, &R3);

	double3 r = double3x3mulv(&R, rₚ);
	// r = double3add(r, double3vs(parent_ref->position.values));

	return r;
}
