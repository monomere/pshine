from math import sqrt, sin, sinh, cos, cosh, tan, atan, nan, pi as π
import dataclasses

@dataclasses.dataclass
class Orbit:
	semimajor: float
	eccentricity: float

@dataclasses.dataclass
class Game:
	time: float

# def propagate(game: Game, orbit: Orbit, Δt: float) -> float:
# 	μ = 1_000
# 	a = orbit.semimajor
# 	e = orbit.eccentricity

# 	p = a * (1 - e*e)

# 	u = nan
# 	if abs(e - 1.0) < 1e-6:
# 		u = 2.0 * sqrt(μ / (p*p*p))
# 	elif e < 1.0:
# 		u = sqrt(μ / (a*a*a))
# 	elif e < 1.0:
# 		u = sqrt(μ / -(a*a*a))
# 	else:
# 		raise RuntimeError("unreachable")

# 	T = 2 * π / u

# 	game.time += Δt
# 	game.time = game.time % T
# 	tsqrtμ = game.time * sqrt(μ)
# 	χ = tsqrtμ/abs(a)
# 	sqrtp = sqrt(a*(1.0 - e*e))
# 	for _ in range(10):
# 		αχ2 = χ*χ/a
# 		sqrtαχ2 = χ*sqrt(abs(a))
# 		Cαχ2 = nan
# 		# Stumpff's C(αχ²)
# 		if abs(αχ2) < 1e-6: Cαχ2 = 0.5
# 		elif αχ2 > 0.0: Cαχ2 = (1 - cos(sqrtαχ2)) / αχ2
# 		else: Cαχ2 = (cosh(sqrtαχ2) - 1) / -αχ2
# 		Sαχ2 = nan
# 		# Stumpff's S(αχ²)
# 		if abs(αχ2) < 1e-6: Sαχ2 = 1./6.
# 		elif αχ2 > 0.0: Sαχ2 = (sqrtαχ2 - sin(sqrtαχ2)) / pow(sqrtαχ2, 3.0)
# 		else: Sαχ2 = (sinh(sqrtαχ2) - sqrtαχ2) / pow(sqrtαχ2, 3.0)
# 		f = sqrtp * χ * χ * Cαχ2 + e * χ*χ*χ * Sαχ2 + a*(1-e) * χ - tsqrtμ
# 		dfdχ = sqrtp * χ * (1.0 - αχ2*Sαχ2) + e * χ*χ * Cαχ2 + a*(1-e)
# 		χ -= f/dfdχ

# 	return χ

import numpy as np

def stumpff_2(z):
	"""Solve the Stumpff function C(z) = c2(z). The input z should be
	a scalar value.
	"""
	if z > 0:
		return (1 - np.cos(np.sqrt(z))) / z
	elif z < 0:
		return (np.cosh(np.sqrt(-z)) - 1) / (-z)
	else:
		return 1/2

def stumpff_3(z):
	"""Solve the Stumpff function S(z) = c3(z). The input z should be
	a scalar value.
	"""
	if z > 0:
		return (np.sqrt(z) - np.sin(np.sqrt(z))) / np.sqrt(z)**3
	elif z < 0:
		return (np.sinh(np.sqrt(-z)) - np.sqrt(-z)) / np.sqrt(-z)**3
	else:
		return 1/6

def universal_kepler(chi: float, r_0: float, p: float, alpha: float, delta_t: float, mu: float):
	"""Solve the universal Kepler equation in terms of the universal anomaly chi.

	This function is intended to be used with an iterative solution algorithm,
	such as Newton's algorithm.
	"""
	z = alpha * chi**2
	first_term = np.sqrt(p) * chi**2 * stumpff_2(z)
	second_term = (1 - alpha * r_0) * chi**3 * stumpff_3(z)
	third_term = r_0 * chi
	fourth_term = np.sqrt(mu) * delta_t
	return first_term + second_term + third_term - fourth_term

def d_universal_d_chi(chi: float, r_0: float, p: float, alpha: float, delta_t: float, mu: float):
	"""The derivative of the universal Kepler equation in terms of the universal anomaly."""
	z = alpha * chi**2
	first_term = np.sqrt(p) * chi * (1 - z * stumpff_3(z))
	second_term = (1 - alpha * r_0) * chi**2 * stumpff_2(z)
	third_term = r_0
	return first_term + second_term + third_term

def find_kepler_solution(game: Game, orbit: Orbit):
	mu = 1000.0
	alpha = 1 / orbit.semimajor
	r_0 = orbit.semimajor * abs(1 - orbit.eccentricity)
	p = orbit.semimajor * abs((1 - orbit.eccentricity**2))
	chi = np.sqrt(mu) * np.abs(alpha) * game.time
	for _ in range(10):
		chi -= universal_kepler(chi, r_0, p, alpha, game.time, mu) / \
			d_universal_d_chi(chi, r_0, p, alpha, game.time, mu)
	return chi

from matplotlib import pyplot as plt

game = Game(0.0)
orbit = Orbit(semimajor=1000, eccentricity=0)
xs = []
ys = []
Δt = 100
for ti in range(0, int(4000/Δt)):
	game.time = ti * Δt
	xs.append(game.time)
	ys.append(find_kepler_solution(game, orbit))

plt.plot(xs, ys)
plt.show()
