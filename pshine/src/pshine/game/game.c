#include "game.h"
#include <string.h>
#include <pshine/util.h>
#include <cimgui/cimgui.h>

static struct keybinds keybinds_arc_movement = {
	.name = "arc_movement",
	.keys = (struct keybind[]){
		[KEYBIND_ARC_ZOOM_IN] = (struct keybind){
			.key = PSHINE_KEY_X,
			.name = "Zoom In",
		},
		[KEYBIND_ARC_ZOOM_OUT] = (struct keybind){
			.key = PSHINE_KEY_Z,
			.name = "Zoom Out",
		},
		[KEYBIND_FLY_PITCH_UP] = (struct keybind){
			.key = PSHINE_KEY_W,
			.name = "Pitch Up",
		},
		[KEYBIND_FLY_PITCH_DOWN] = (struct keybind){
			.key = PSHINE_KEY_S,
			.name = "Pitch Down",
		},
		[KEYBIND_FLY_YAW_LEFT] = (struct keybind){
			.key = PSHINE_KEY_A,
			.name = "Yaw Left",
		},
		[KEYBIND_FLY_YAW_RIGHT] = (struct keybind){
			.key = PSHINE_KEY_D,
			.name = "Yaw Right",
		},
	},
};

static struct keybinds keybinds_fly_movement = {
	.name = "fly_movement",
	.keys = (struct keybind[]){
		[KEYBIND_FLY_MOVE_FORWARD] = (struct keybind){
			.key = PSHINE_KEY_W,
			.name = "Move Forward",
		},
		[KEYBIND_FLY_MOVE_BACKWARD] = (struct keybind){
			.key = PSHINE_KEY_S,
			.name = "Move Backward",
		},
		[KEYBIND_FLY_MOVE_LEFT] = (struct keybind){
			.key = PSHINE_KEY_A,
			.name = "Move Left",
		},
		[KEYBIND_FLY_MOVE_RIGHT] = (struct keybind){
			.key = PSHINE_KEY_D,
			.name = "Move Right",
		},
		[KEYBIND_FLY_MOVE_UP] = (struct keybind){
			.key = PSHINE_KEY_Q,
			.name = "Move Up",
		},
		[KEYBIND_FLY_MOVE_DOWN] = (struct keybind){
			.key = PSHINE_KEY_E,
			.name = "Move Down",
		},
		[KEYBIND_FLY_PITCH_UP] = (struct keybind){
			.key = PSHINE_KEY_UP,
			.name = "Pitch Up",
		},
		[KEYBIND_FLY_PITCH_DOWN] = (struct keybind){
			.key = PSHINE_KEY_DOWN,
			.name = "Pitch Down",
		},
		[KEYBIND_FLY_YAW_LEFT] = (struct keybind){
			.key = PSHINE_KEY_LEFT,
			.name = "Yaw Left",
		},
		[KEYBIND_FLY_YAW_RIGHT] = (struct keybind){
			.key = PSHINE_KEY_RIGHT,
			.name = "Yaw Right",
		},
	},
};

static struct keybinds keybinds_ship_movement = {
	.name = "ship_movement",
	.keys = (struct keybind[]){
		[KEYBIND_SHIP_THROTTLE_INC] = (struct keybind){
			.key = PSHINE_KEY_LEFT_SHIFT,
			.name = "Throttle Increase",
		},
		[KEYBIND_SHIP_THROTTLE_DEC] = (struct keybind){
			.key = PSHINE_KEY_LEFT_CONTROL,
			.name = "Throttle Decrease",
		},
		[KEYBIND_SHIP_PITCH_UP] = (struct keybind){
			.key = PSHINE_KEY_S,
			.name = "Pitch Up",
		},
		[KEYBIND_SHIP_PITCH_DOWN] = (struct keybind){
			.key = PSHINE_KEY_W,
			.name = "Pitch Down",
		},
		[KEYBIND_SHIP_ROLL_LEFT] = (struct keybind){
			.key = PSHINE_KEY_E,
			.name = "Roll Right",
		},
		[KEYBIND_SHIP_ROLL_RIGHT] = (struct keybind){
			.key = PSHINE_KEY_Q,
			.name = "Roll Left",
		},
		[KEYBIND_SHIP_YAW_LEFT] = (struct keybind){
			.key = PSHINE_KEY_A,
			.name = "Yaw Left",
		},
		[KEYBIND_SHIP_YAW_RIGHT] = (struct keybind){
			.key = PSHINE_KEY_D,
			.name = "Yaw Right",
		},
	},
};

static void init_star_system(struct pshine_game *game, struct pshine_star_system *system) {
	for (size_t i = 0; i < system->body_count; ++i) {
		const char *name = system->bodies_own[i]->tmp_parent_ref_name_own;
		if (name == nullptr) continue;
		for (size_t j = 0; j < system->body_count; ++j) {
			if (strcmp(system->bodies_own[j]->name_own, name) == 0) {
				system->bodies_own[i]->parent_ref = system->bodies_own[j];
				// PSHINE_DEBUG(
				// 	"Setting parent_ref of %s to %s",
				// 	system->bodies_own[i]->name_own,
				// 	name
				// );
				break;
			}
		}
		if (system->bodies_own[i]->parent_ref == nullptr) {
			PSHINE_ERROR(
				"No celestial body with name '%s' found (setting parent reference for %s)",
				name,
				system->bodies_own[i]->name_own
			);
		}
	}

	for (size_t i = 0; i < system->body_count; ++i) {
		if (!system->bodies_own[i]->is_static) {
			create_orbit_points(system->bodies_own[i], 1000);
		}
	}
}

void pshine_init_game(struct pshine_game *game) {
	{
		struct pshine_timeval now = pshine_timeval_now();
		pshine_pcg64_init(&game->rng64, now.sec, now.nsec);
	}
	game->time_scale = 1.0;
	game->data_own = calloc(1, sizeof(struct pshine_game_data));
	load_game_config(game, "data/config.toml");

	for (size_t i = 0; i < game->star_system_count; ++i) {
		init_star_system(game, &game->star_systems_own[i]);
	}

	{
		size_t idx = PSHINE_DYNA_ALLOC(game->ships);
		game->ships.ptr[idx]._alive_marker = (size_t)-1;
		game->ships.ptr[idx].name_own = pshine_strdup("Red Menace");
		game->ships.ptr[idx].callcode_own = pshine_strdup("NG-XK-AP-421620");
		game->ships.ptr[idx].model_file_own = pshine_strdup("data/models/red_menace.glb");
		game->ships.ptr[idx].position.xyz.x = 23058677.647 * PSHINE_SCS_SCALE;
		game->ships.ptr[idx].position.xyz.y = -363.291 * PSHINE_SCS_SCALE;
		game->ships.ptr[idx].position.xyz.z = 10228938.562 * PSHINE_SCS_SCALE;
		*(floatR*)game->ships.ptr[idx].orientation.values = floatReuler(0, 0, 0);
		game->ships.ptr[idx].scale = 4.0;
		game->ships.ptr[idx].max_atmo_velocity = 550.0;
		game->ships.ptr[idx].max_space_velocity = 18600.0;
	}

	if (game->star_system_count <= 0) {
		PSHINE_PANIC("No star systems present, there's nothing to show; exiting.");
	}
	game->current_star_system = 2;
	game->data_own->selected_body = 0;
	game->data_own->camera_dist = game->star_systems_own[game->current_star_system].bodies_own[0]->radius + 165'000'000.0;
	game->camera_position.xyz.z = -game->data_own->camera_dist;
	game->data_own->camera_yaw = π/2;
	game->data_own->camera_pitch = 0.0;
	memset(game->data_own->last_key_states, 0, sizeof(game->data_own->last_key_states));
	game->atmo_blend_factor = 0.0;
	game->data_own->movement_mode = MOVEMENT_SHIP;
	game->data_own->move_speed = 500'000.0;
	game->time_scale = 0.0;
	game->camera_position.xyz.x = 31483290.911 * PSHINE_SCS_SCALE;
	game->camera_position.xyz.y = 75.221 * PSHINE_SCS_SCALE;
	game->camera_position.xyz.z = 13965308.151 * PSHINE_SCS_SCALE;
	game->graphics_settings.camera_fov = 60.0;
	game->graphics_settings.exposure = 1.0;
	game->graphics_settings.bloom_knee = 7.0;
	game->graphics_settings.bloom_threshold = 7.0;
	game->material_smoothness_ = 0.02;
	game->data_own->is_control_precise = false;
	game->data_own->ship_camera_data.target_distance = 50.0;
	
	game->data_own->keybinds_ship_movement = &keybinds_ship_movement;
	game->data_own->keybinds_arc_movement = &keybinds_arc_movement;
	game->data_own->keybinds_fly_movement = &keybinds_fly_movement;
}

static void deinit_star(struct pshine_star *star) {
	(void)star;
}

static void deinit_planet(struct pshine_planet *planet) {
	(void)planet;
}

static void deinit_star_system(struct pshine_game *game, struct pshine_star_system *system) {
	for (size_t i = 0; i < system->body_count; ++i) {
		struct pshine_celestial_body *b = system->bodies_own[i];
		if (b == NULL) {
			PSHINE_WARN("null celestial body");
			continue;
		}
		switch (b->type) {
			case PSHINE_CELESTIAL_BODY_PLANET: deinit_planet((void*)b); break;
			case PSHINE_CELESTIAL_BODY_STAR: deinit_star((void*)b); break;
			default: PSHINE_PANIC("bad b->type: %d", (int)b->type); break;
		}
#define FREE_IF_NOTNULL(X) if ((X) != nullptr) free((X))
		FREE_IF_NOTNULL(b->surface.albedo_texture_path_own);
		FREE_IF_NOTNULL(b->surface.spec_texture_path_own);
		FREE_IF_NOTNULL(b->surface.lights_texture_path_own);
		FREE_IF_NOTNULL(b->surface.bump_texture_path_own);
		FREE_IF_NOTNULL(b->name_own);
		FREE_IF_NOTNULL(b->desc_own);
		FREE_IF_NOTNULL(b->tmp_parent_ref_name_own);
		FREE_IF_NOTNULL(b->orbit.cached_points_own);
#undef FREE_IF_NOTNULL
		free(b);
	}
	free(system->bodies_own);
	free(system->name_own);
}

void pshine_deinit_game(struct pshine_game *game) {
	eximgui_deinit();
	for (size_t i = 0; i < game->star_system_count; ++i) {
		deinit_star_system(game, &game->star_systems_own[i]);
	}
	free(game->star_systems_own);
	free(game->data_own);
}

[[maybe_unused]]
static void update_camera_walk(struct pshine_game *game, float delta_time) {
	// Unlike the Fly mode, we need to select a proper basis for our pitch/yaw rotation.
	// One of the axes is easy, the normal of the planet sphere, the other one a bit more complicated.
	// (we can get the third axis by doing a cross product of the other axes)
}

[[maybe_unused]]
static void update_camera_fly(struct pshine_game *game, float delta_time) {
	const struct keybinds *kbd = game->data_own->keybinds_fly_movement;

	{
		double3 delta = {};
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_YAW_LEFT].key)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_YAW_RIGHT].key)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_PITCH_UP].key)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_PITCH_DOWN].key)) delta.y -= 1.0;
		double rot_speed = 1.0 * (game->data_own->is_control_precise ? 0.01 : 1.0);
		game->data_own->camera_pitch += delta.y * rot_speed * delta_time;
		game->data_own->camera_yaw += delta.x * rot_speed * delta_time;
	}

	floatR cam_rot = floatReuler(-game->data_own->camera_pitch, -game->data_own->camera_yaw, 0);
	double3 cam_pos = double3vs(game->camera_position.values);
	{
		float3 delta = {};
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_RIGHT].key)) delta.x += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_LEFT].key)) delta.x -= 1.0;
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_UP].key)) delta.y += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_DOWN].key)) delta.y -= 1.0;
		if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_FORWARD].key)) delta.z += 1.0;
		else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_FLY_MOVE_BACKWARD].key)) delta.z -= 1.0;
		delta = floatRapply(cam_rot, delta);
		cam_pos = double3add(cam_pos, double3mul(double3norm(double3_float3(delta)),
			game->data_own->move_speed * delta_time));
	}

	*(double3*)game->camera_position.values = cam_pos;
	*(floatR*)game->camera_orientation.values = cam_rot;
}

[[maybe_unused]]
static void update_camera_arc(struct pshine_game *game, float delta_time) {
	double3 delta = {};
	const struct keybinds *kbd = game->data_own->keybinds_fly_movement;
	if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_YAW_LEFT].key)) delta.x += 1.0;
	else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_YAW_RIGHT].key)) delta.x -= 1.0;
	if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_PITCH_UP].key)) delta.y += 1.0;
	else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_PITCH_DOWN].key)) delta.y -= 1.0;

	if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_ZOOM_IN].key))
		game->data_own->camera_dist += game->data_own->move_speed * delta_time;
	else if (pshine_is_key_down(game->renderer, kbd->keys[KEYBIND_ARC_ZOOM_OUT].key))
		game->data_own->camera_dist -= game->data_own->move_speed * delta_time;

	double rot_speed = 1.0 * (game->data_own->is_control_precise ? 0.01 : 1.0);
	game->data_own->camera_pitch += delta.y * rot_speed * delta_time;
	game->data_own->camera_yaw += delta.x * rot_speed * delta_time;

	game->data_own->camera_pitch = clampd(game->data_own->camera_pitch, -π/2 + 0.1, π/2 - 0.1);

	double3 cam_pos = double3mul(double3xyz(
		cos(game->data_own->camera_pitch) * sin(game->data_own->camera_yaw),
		sin(game->data_own->camera_pitch),
		-cos(game->data_own->camera_pitch) * cos(game->data_own->camera_yaw)
	), game->data_own->camera_dist);

	double3 b_pos = double3vs(game->star_systems_own[game->current_star_system].bodies_own[game->data_own->selected_body]->position.values);
	cam_pos = double3add(cam_pos, b_pos);
	double3 cam_forward = double3norm(double3sub(b_pos, cam_pos));
	// double3 cam_forward = double3xyz(-1.0f, 0.0f, 0.0f);

	*(double3*)game->camera_position.values = cam_pos;
	*(floatR*)game->camera_orientation.values = floatRfromto(float3xyz(0, 0, 1), float3_double3(cam_forward));
}

static void update_celestial_body(struct pshine_game *game, float delta_time, struct pshine_celestial_body *body) {
	if (!body->is_static) {
		propagate_orbit(delta_time, body->parent_ref->gravitational_parameter, &body->orbit);
		body->rotation += body->rotation_speed * delta_time;
		body->rotation = fmod(body->rotation, 2 * π);
		double3 position = kepler_orbit_to_state_vector(&body->orbit);
		position = double3add(position, double3vs(body->parent_ref->position.values));
		*(double3*)&body->position = position;
	}
}

void pshine_post_init_game(struct pshine_game *game) {
	eximgui_init();
}

static void update_star_system(struct pshine_game *game, struct pshine_star_system *system, float delta_time) {
	for (size_t i = 0; i < system->body_count; ++i) {
		update_celestial_body(game, delta_time, system->bodies_own[i]);
	}
}

static void update_camera_ship(struct pshine_game *game, float delta_time) {
	update_ship(game, &game->ships.ptr[0], delta_time);

	struct pshine_ship *ship = &game->ships.ptr[0];
	floatR ship_orient = floatRvs(ship->orientation.values);

	float3 ship_forward = floatRapply(ship_orient, float3xyz(0, 0, 1));

	if (pshine_is_mouse_down(game->renderer, 1)) {
		game->data_own->ship_camera_data.pitch += game->data_own->mouse_pos_delta.y * 0.001;
		game->data_own->ship_camera_data.yaw += game->data_own->mouse_pos_delta.x * 0.001;
	}

	{
		double scroll = 0.0;
		pshine_get_mouse_scroll_delta(game->renderer, nullptr, &scroll);
		game->data_own->ship_camera_data.target_distance -= scroll * 10.0;
		game->data_own->ship_camera_data.distance
			= lerpd(game->data_own->ship_camera_data.distance, game->data_own->ship_camera_data.target_distance, 0.5);
	}

	floatR cam_orient = floatReuler(
		game->data_own->ship_camera_data.pitch,
		game->data_own->ship_camera_data.yaw,
		0.0
	);
	floatR cam_global_orient = floatRcombine(ship_orient, cam_orient);
	float3 cam_forward = floatRapply(cam_global_orient, float3xyz(0, 0, 1));
	
	float3 cam_up = floatRapply(cam_global_orient, float3xyz(0, 1, 0));
	float3 cam_right = floatRapply(cam_global_orient, float3xyz(1, 0, 0));
	
	float shake_x = pshine_pcg64_random_float(&game->rng64);
	float shake_y = pshine_pcg64_random_float(&game->rng64);
	float shake_intensity = powf((ship->velocity / ship->max_space_velocity), 5) * 0.2;

	float3 cam_shake
		= float3add(float3mul(cam_right, shake_x * shake_intensity), float3mul(cam_up, shake_y * shake_intensity));

	double3 pos = double3vs(ship->position.values);

	double speed_distance_offset = ship->velocity / ship->max_space_velocity * 5.;
	game->actual_camera_fov += ship->velocity / ship->max_space_velocity * 25. * float3dot(cam_forward, ship_forward);
	double3 cam_offset =
		double3add(
			double3mul(
				double3_float3(cam_forward), 
				-game->data_own->ship_camera_data.distance - speed_distance_offset
			),
			double3_float3(cam_shake)
		);

	*(double3*)game->camera_position.values = double3add(cam_offset, pos);
	*(floatR*)game->camera_orientation.values = cam_global_orient;
		// floatRfromto(float3xyz(0, 0, 1), float3_double3(forward));
}

void pshine_update_game(struct pshine_game *game, float actual_delta_time) {
	game->time += actual_delta_time * game->time_scale;

	double2 mouse_pos;
	pshine_get_mouse_position(game->renderer, &mouse_pos.x, &mouse_pos.y);
	game->data_own->mouse_pos_delta = double2sub(mouse_pos, game->data_own->last_mouse_pos);
	game->data_own->last_mouse_pos = mouse_pos;

	game->actual_camera_fov = game->graphics_settings.camera_fov;

	for (size_t i = 0; i < game->star_system_count; ++i) {
		update_star_system(game, &game->star_systems_own[i], actual_delta_time * game->time_scale);
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_P) && !game->data_own->last_key_states[PSHINE_KEY_P]) {
		game->data_own->is_control_precise = !game->data_own->is_control_precise;
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_F) && !game->data_own->last_key_states[PSHINE_KEY_F]) {
		game->data_own->movement_mode = (game->data_own->movement_mode + 1) % MOVEMENT_COUNT_;
	}

	if (pshine_is_key_down(game->renderer, PSHINE_KEY_F2) && !game->data_own->last_key_states[PSHINE_KEY_F2]) {
		if (pshine_is_key_down(game->renderer, PSHINE_KEY_LEFT_SHIFT)) {
			game->ui_dont_render_windows = !game->ui_dont_render_windows;
		} else {
			game->ui_dont_render_windows = game->ui_dont_render_gizmos = !game->ui_dont_render_gizmos;
		}

	}

	switch (game->data_own->movement_mode) {
		case MOVEMENT_ARC: update_camera_arc(game, actual_delta_time); break;
		case MOVEMENT_FLY: update_camera_fly(game, actual_delta_time); break;
		case MOVEMENT_WALK: update_camera_walk(game, actual_delta_time); break;
		case MOVEMENT_SHIP: update_camera_ship(game, actual_delta_time); break;
		default:
			PSHINE_WARN("Unknown movement mode: %d, switching to fly", game->data_own->movement_mode);
			game->data_own->movement_mode = MOVEMENT_FLY;
			break;
	}

	memcpy(game->data_own->last_key_states, pshine_get_key_states(game->renderer), sizeof(uint8_t) * PSHINE_KEY_COUNT_);

	if (!game->ui_dont_render_windows) draw_debug_windows(game, actual_delta_time);
}
