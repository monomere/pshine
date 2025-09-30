#include "game.h"
#include <float.h>
#include "../audio.h"

static double sphere_ray_intersection(
	double3 ro,
	double3 rd,
	double3 sc,
	double sr
) {
	ro = double3sub(ro, sc);
	double b = double3dot(ro, double3norm(rd));
	double c = double3dot(ro, ro) - sr * sr;
	double d = b * b - c;

	if (d >= 0.0) {
		double s = sqrt(d);
		// float near = maxd(0.0, (-b - s));
		// float far = (-b + s);
		if (-b + s > 0.0) return maxd(0.0, -b - s);
	}

	return INFINITY;
}

void update_ship(struct pshine_game *game, struct pshine_ship *ship, float delta_time) {
	{
		double min_dist = DBL_MAX;
		for (size_t i = 0; i < game->star_systems_own[game->current_star_system].body_count; ++i) {
			struct pshine_celestial_body *body = game->star_systems_own[game->current_star_system].bodies_own[i];
			double3 body_pos = double3vs(body->position.values);
			double3 ship_pos = double3vs(ship->position.values);
			double dist = double3mag2(double3sub(body_pos, ship_pos));
			if (dist < min_dist) {
				min_dist = dist;
				ship->closest_body = body;
				ship->closest_body_distance = sqrt(dist);
			}
		}
	}


	struct keybinds *kbd = game->data_own->keybinds_ship_movement;
	floatR ship_orient = floatRvs(ship->orientation.values);
	double3 ship_pos = double3vs(ship->position.values);
	bool precice_control = game->data_own->is_control_precise || ship->is_in_warp;
	{
		float3 delta = {};
		
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_PITCH_UP].key)) delta.y += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_PITCH_DOWN].key)) delta.y -= 1.0f;

		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_YAW_RIGHT].key)) delta.x += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_YAW_LEFT].key)) delta.x -= 1.0f;
		
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_ROLL_RIGHT].key)) delta.z += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_ROLL_LEFT].key)) delta.z -= 1.0f;

		float rot_speed = 1.0f * (precice_control ? 0.01f : 1.0f);
		
		float pitch = -delta.y * rot_speed * delta_time;
		float yaw = delta.x * rot_speed * delta_time;
		float roll = delta.z * rot_speed * delta_time;
		floatR delta_orient = floatReuler(pitch, yaw, roll);
		ship_orient = floatRcombine(ship_orient, delta_orient);
	}

	static double safe_warp_radius = 1.1;
	ship->is_warp_safe = ship->closest_body_distance >= ship->closest_body->radius * safe_warp_radius;

	if (
		ship->is_warp_safe &&
		pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_WARP].key) &&
		!game->data_own->last_key_states[kbd->keys[KEYBIND_SHIP_WARP].key]
	) ship->is_in_warp = !ship->is_in_warp;

	if (!ship->is_in_warp) {
		double delta = 0.0f;
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_THROTTLE_INC].key)) delta += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_THROTTLE_DEC].key)) delta -= 1.0;
		if (ship->velocity < 0.0001 && delta > 0.0001) {
			pshine_sound sound = pshine_create_sound_from_file(game->data_own->audio, &(struct pshine_sound_info){
				.name = "data/audio/laser.wav",
				.quiet = 0.9f,
			});
			pshine_play_sound(game->data_own->audio, sound);
		}
		delta *= delta_time;
		ship->velocity += delta * 5000.0;
		double height = 0.0; /* 0-1 */
		bool inside_atmosphere = false;
		if (ship->closest_body->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *planet = (void*)ship->closest_body;
			height = ship->closest_body_distance - ship->closest_body->radius;
			height /= planet->atmosphere.height;
			inside_atmosphere = 0.0 <= height && height <= 1.0;
		}
		if (inside_atmosphere) {
			double t = exp(-height) * (1 - height);
			ship->current_max_velocity = lerpd(ship->max_space_velocity, ship->max_atmo_velocity, t * t);
		} else {
			ship->current_max_velocity = ship->max_space_velocity;
		}
		ship->velocity = clampd(ship->velocity, 0, ship->current_max_velocity);
	} else {
		// We can't check the distance like this since we might go through the body in a single frame.
		// if (ship->closest_body_distance <= ship->closest_body->radius * 1.5)
		// 	ship->is_in_warp = false;

		// Instead we check the intersection of ship's "velocity ray" and the body's sphere.
		
		double3 dir = double3_float3(floatRapply(ship_orient, float3xyz(0, 0, 1)));
		static const double scaling_factor = 0.000001; // m/m
		double3 scaled_ship_pos = double3mul(double3vs(ship->position.values), scaling_factor);
		double3 scaled_body_pos = double3mul(double3vs(ship->closest_body->position.values), scaling_factor);
		double t = sphere_ray_intersection(
			scaled_ship_pos,
			dir,
			scaled_body_pos, // TODO: The closest body might change in a single frame too!
			ship->closest_body->radius * safe_warp_radius * scaling_factor
		);

		double scaled_travel = ship->max_warp_velocity * delta_time * scaling_factor;
		if (!isinf(t) && 0.0 <= t && t <= scaled_travel) {
			ship->velocity = 0.0;
			PSHINE_DEBUG("Emergency warp stop, t = %f, scaled_travel = %f", t, scaled_travel);
			PSHINE_DEBUG("-> %f", t / scaling_factor);
			ship_pos = double3div(double3add(scaled_ship_pos, double3mul(dir, t)), scaling_factor);
			ship->is_warp_safe = false;
		} else {
			ship->velocity = ship->max_warp_velocity;
			ship->is_warp_safe = true;
		}
	}
	if (!ship->is_warp_safe) ship->is_in_warp = false;
	{
		float3 delta = floatRapply(ship_orient, float3xyz(0, 0, 1));
		ship_pos = double3add(ship_pos, double3mul(double3_float3(delta), ship->velocity * delta_time));
	}
	*(double3*)ship->position.values = ship_pos;
	*(floatR*)ship->orientation.values = ship_orient;
}
