#ifndef GAME_H_INCLUDED_
#define GAME_H_INCLUDED_
#include <pshine/game.h>
#include "../math.h"

enum si_prefix {
	SI_ONE,
	SI_KILO,
	SI_MEGA,
	SI_GIGA,
	SI_PETA,
	SI_EXA,
	SI_ZETTA,
	SI_YOTTA,
	SI_RONNA,
	SI_QUETTA,
};

static enum si_prefix find_optimal_si_prefix(double value) {
	value = fabs(value);
	if (value < 1'000.0) return SI_ONE;
	if (value < 1'000'000.0) return SI_KILO;
	if (value < 1'000'000'000.0) return SI_MEGA;
	if (value < 1'000'000'000'000.0) return SI_GIGA;
	if (value < 1'000'000'000'000'000.0) return SI_PETA;
	return SI_EXA;
}

[[maybe_unused]]
static const char *si_prefix_string(enum si_prefix p) {
	switch (p) {
	case SI_ONE:    return "";
	case SI_KILO:   return "k";
	case SI_MEGA:   return "M";
	case SI_GIGA:   return "G";
	case SI_PETA:   return "P";
	case SI_EXA:    return "E";
	case SI_ZETTA:  return "Z";
	case SI_YOTTA:  return "Y";
	case SI_RONNA:  return "R";
	case SI_QUETTA: return "Q";
	}
	return "";
}

static const char *si_prefix_english(enum si_prefix p) {
	switch (p) {
	case SI_ONE:    return "";
	case SI_KILO:   return "thousand";
	case SI_MEGA:   return "million";
	case SI_GIGA:   return "billion";
	case SI_PETA:   return "trillion";
	case SI_EXA:    return "quadrillion";
	case SI_ZETTA:  return "quntillion";
	case SI_YOTTA:  return "sextillion";
	case SI_RONNA:  return "septillion";
	case SI_QUETTA: return "octillion";
	}
	return "";
}

static double apply_si_prefix(enum si_prefix p, double value) {
	switch (p) {
	case SI_ONE:    return value;
	case SI_KILO:   return value / 1'000.0;
	case SI_MEGA:   return value / 1'000'000.0;
	case SI_GIGA:   return value / 1'000'000'000.0;
	case SI_PETA:   return value / 1'000'000'000'000.0;
	case SI_EXA:    return value / 1'000'000'000'000'000.0;
	case SI_ZETTA:  return value / 1'000'000'000'000'000'000.0;
	case SI_YOTTA:  return value / 1'000'000'000'000'000'000'000.0;
	case SI_RONNA:  return value / 1'000'000'000'000'000'000'000'000.0;
	case SI_QUETTA: return value / 1'000'000'000'000'000'000'000'000'000.0;
	}
	return value;
}

#define TIME_FORMAT "%.0fy %.0fmo %.0fd %.0fh:%.0fm:%.3fs"
struct time_format_params {
	double years;
	double months;
	double days;
	double hours;
	double minutes;
	double seconds;
};
#define TIME_FORMAT_ARGS(P) ((P).years), ((P).months), ((P).days), ((P).hours), ((P).minutes), ((P).seconds)
static struct time_format_params compute_time_format_params(double secs) {
	struct time_format_params r = {0};
	r.seconds = fabs(secs);
	r.minutes = trunc(r.seconds / 60.0); r.seconds = fmod(r.seconds, 60.0);
	r.hours = trunc(r.minutes / 60.0); r.minutes = fmod(r.minutes, 60.0);
	r.days = trunc(r.hours / 24.0); r.hours = fmod(r.hours, 24.0);
	r.months = trunc(r.days * 12.0 / 365.0); r.days = fmod(r.days, 365.0 / 12.0);
	r.years = trunc(r.months / 12.0); r.months = fmod(r.months, 12.0);
	return r;
}

struct keybind {
	enum pshine_key key;
	const char *name;
};

struct keybinds {
	const char *name;
	struct keybind *keys;
};

enum {
	KEYBIND_ARC_ZOOM_IN,
	KEYBIND_ARC_ZOOM_OUT,
	KEYBIND_ARC_PITCH_UP,
	KEYBIND_ARC_PITCH_DOWN,
	KEYBIND_ARC_YAW_LEFT,
	KEYBIND_ARC_YAW_RIGHT,
};

enum {
	KEYBIND_FLY_MOVE_FORWARD,
	KEYBIND_FLY_MOVE_BACKWARD,
	KEYBIND_FLY_MOVE_LEFT,
	KEYBIND_FLY_MOVE_RIGHT,
	KEYBIND_FLY_MOVE_UP,
	KEYBIND_FLY_MOVE_DOWN,
	KEYBIND_FLY_PITCH_UP,
	KEYBIND_FLY_PITCH_DOWN,
	KEYBIND_FLY_YAW_LEFT,
	KEYBIND_FLY_YAW_RIGHT,
};

enum {
	KEYBIND_SHIP_THROTTLE_INC,
	KEYBIND_SHIP_THROTTLE_DEC,
	KEYBIND_SHIP_PITCH_UP,
	KEYBIND_SHIP_PITCH_DOWN,
	KEYBIND_SHIP_ROLL_LEFT,
	KEYBIND_SHIP_ROLL_RIGHT,
	KEYBIND_SHIP_YAW_LEFT,
	KEYBIND_SHIP_YAW_RIGHT,
};

void propagate_orbit(
	double delta_time,
	double gravitational_parameter,
	struct pshine_orbit_info *orbit
);

void create_orbit_points(
	struct pshine_celestial_body *body,
	size_t count
);

double3 kepler_orbit_to_state_vector(
	const struct pshine_orbit_info *orbit
);

struct eximgui_state;

void eximgui_init();
void eximgui_deinit();
void init_imgui_style();
void eximgui_begin_frame();
void eximgui_end_frame();

bool eximgui_input_double3(const char *label, const char *tooltip, double *vs, double step, const char *format);
bool eximgui_begin_input_box(const char *label, const char *tooltip);
void eximgui_end_input_box();

void eximgui_title_text(const char *fmt, ...);
void eximgui_bold_text(const char *fmt, ...);

struct eximgui_plot_info {
	float x_min, x_max;
	size_t x_ticks, y_ticks;
	size_t y_count;
	const float *ys;
	bool auto_y_range;
	bool extend_by_auto_range;
	float y_min, y_max;
};

void eximgui_plot(const struct eximgui_plot_info *info);

enum movement_mode {
	MOVEMENT_FLY,
	MOVEMENT_ARC,
	MOVEMENT_WALK,
	MOVEMENT_SHIP,
	MOVEMENT_COUNT_,
};

struct ship_camera_data {
	float yaw, pitch;
	double distance;
	double target_distance;
};

struct pshine_game_data {
	double camera_yaw, camera_pitch;
	double camera_dist;
	struct ship_camera_data ship_camera_data;
	enum movement_mode movement_mode;
	double move_speed;
	uint8_t last_key_states[PSHINE_KEY_COUNT_];
	size_t selected_body;
	bool is_control_precise;
	struct keybinds *keybinds_ship_movement;
	struct keybinds *keybinds_fly_movement;
	struct keybinds *keybinds_arc_movement;

	double2 mouse_pos;
	double2 last_mouse_pos;
	double2 mouse_pos_delta;
};

void load_game_config(struct pshine_game *game, const char *fpath);
void draw_debug_windows(struct pshine_game *game, float actual_delta_time);

void update_ship(struct pshine_game *game, struct pshine_ship *ship, float actual_delta_time);

#endif // GAME_H_INCLUDED_
