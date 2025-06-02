#include "game.h"
#include <float.h>
#include "../audio.h"

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
	{
		float3 delta = {};
		
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_PITCH_UP].key)) delta.y += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_PITCH_DOWN].key)) delta.y -= 1.0f;

		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_YAW_RIGHT].key)) delta.x += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_YAW_LEFT].key)) delta.x -= 1.0f;
		
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_ROLL_RIGHT].key)) delta.z += 1.0f;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_ROLL_LEFT].key)) delta.z -= 1.0f;

		float rot_speed = 1.0f * (game->data_own->is_control_precise ? 0.01f : 1.0f);
		
		float pitch = -delta.y * rot_speed * delta_time;
		float yaw = delta.x * rot_speed * delta_time;
		float roll = delta.z * rot_speed * delta_time;
		floatR delta_orient = floatReuler(pitch, yaw, roll);
		ship_orient = floatRcombine(ship_orient, delta_orient);
	}
	
	{
		double delta = 0.0f;
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_THROTTLE_INC].key)) delta += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_SHIP_THROTTLE_DEC].key)) delta -= 1.0;
		delta *= delta_time * 1000.0f;
		if (ship->velocity < 0.0001 && delta > 0.0001) {
			pshine_create_sound_from_file(game->data_own->audio, &(struct pshine_sound_info){
				.name = "data/audio/engine_start.flac",
				.quiet = 0.0f,
			});
		}
		ship->velocity += delta * 5.0;
		double height = 0.0; /* 0-1 */
		bool inside_atmosphere = false;
		if (ship->closest_body->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *planet = (void*)ship->closest_body;
			height = ship->closest_body_distance - ship->closest_body->radius;
			height /= planet->atmosphere.height;
			inside_atmosphere = height <= 1.0;
		}
		if (inside_atmosphere) {
			double t = exp(-height) * (1 - height);
			ship->current_max_velocity = lerpd(ship->max_space_velocity, ship->max_atmo_velocity, t * t);
		} else {
			ship->current_max_velocity = ship->max_space_velocity;
		}
		ship->velocity = clampd(ship->velocity, 0, ship->current_max_velocity);
	}
	{
		float3 delta = floatRapply(ship_orient, float3xyz(0, 0, 1));
		ship_pos = double3add(ship_pos, double3mul(double3_float3(delta), ship->velocity * delta_time));
	}
	*(double3*)ship->position.values = ship_pos;
	*(floatR*)ship->orientation.values = ship_orient;
}
