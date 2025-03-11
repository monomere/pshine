#ifndef PSHINE_GAME_H_
#define PSHINE_GAME_H_
#include <stdint.h>
#include <stddef.h>

typedef union pshine_point3d_ {
	struct { double x, y, z; } xyz;
	double values[3];
} pshine_point3d;

typedef union pshine_vector3d_ {
	struct { double x, y, z; } xyz;
	double values[3];
} pshine_vector3d;

// Scaled Coordinate Space Scale
#define PSHINE_SCS_SCALE 6000.0
// Scaled Coordinate Space Factor (`1.0 / PSHINE_SCS_SCALE`)
#define PSHINE_SCS_FACTOR (1.0 / PSHINE_SCS_SCALE)

#define PSHINE_SPEED_OF_LIGHT 299'792'458.0

// 1:6000
typedef union pshine_point3d_scaled_ {
	struct { double x, y, z; } xyz;
	double values[3];
} pshine_point3d_scaled;

// 1:1
typedef union pshine_point3d_world_ {
	struct { double x, y, z; } xyz;
	double values[3];
} pshine_point3d_world;

/// Defines a Keplerian orbit. Also contains the cached
/// points along the orbit path, for rendering the trajectory.
struct pshine_orbit_info {
	float
		inclination, // inclination, rad (i)
		longitude, // longitude of the ascending node, rad (Ω)
		argument, // argument of periapsis, rad (ω)
		eccentricity, // eccentricity, 1 (e)
		semimajor, // semimajor axis, Mm (megameters) (a)
		true_anomaly; // true anomaly, rad (ν)

	/// Count of `this->cached_points_own`.
	size_t cached_point_count;

	/// Cached points spaced along the trajectory (for hyperbolic trajectories, TODO)
	pshine_point3d_scaled *cached_points_own;
};

enum pshine_celestial_body_type {
	PSHINE_CELESTIAL_BODY_UNINITIALIZED,
	PSHINE_CELESTIAL_BODY_PLANET,
	PSHINE_CELESTIAL_BODY_STAR,
};

struct pshine_rings_info {
	bool has_rings;
	char *slice_texture_path_own;
	double inner_radius;
	double outer_radius;
};

struct pshine_surface_info {
	char *albedo_texture_path_own;
	char *bump_texture_path_own;
	char *lights_texture_path_own;
	char *spec_texture_path_own;
};

struct pshine_celestial_body {
	enum pshine_celestial_body_type type;
	struct pshine_celestial_body *parent_ref;
	struct pshine_orbit_info orbit;
	struct pshine_surface_info surface;
	struct pshine_rings_info rings;

	/// Mean radius, in m.
	/// Some values:
	/// - Earth: `6'371'000.0`
	/// - Sun: `695'700'000.0`
	double radius;

	/// If true, the orbit information is ignored.
	bool is_static;

	/// Position relative to the origin (usually the Sun)
	pshine_point3d_world position;

	/// In rad/h
	double rotation_speed;

	/// Axis of rotation (should be normalized)
	pshine_vector3d rotation_axis;

	/// Current rotation around `rotation_axis`, in radians.
	double rotation;

	/// Mass of the body, in Earth masses (5.9742×10²⁴ kg)
	/// Some values:
	/// - Sun: `332'950.0`
	/// - Jupiter: `317.8`
	/// - Earth: `1.0` (crazy)
	double mass;

	/// The standard gravitational parameter, in Mm³s⁻² (Mm = megameter).
	/// Some values:
	/// - Sun: `132.71244`
	/// - Earth: `3.98600436e-4`
	double gravitational_parameter;

	/// 0xBBGGRR
	uint32_t gizmo_color;

	/// 0xBBGGRR
	uint32_t average_color;

	/// e.g. "Sun", "Earth".
	char *name_own;

	/// Name of parent_ref, used during initialization, might be invalid.
	char *tmp_parent_ref_name_own;
};

struct pshine_atmosphere_info {
	/// In m
	double height;
	float rayleigh_coefs[3];
	float rayleigh_falloff;
	float mie_coef;
	float mie_ext_coef;
	float mie_g_coef;
	float mie_falloff;
	float intensity;
};

struct pshine_planet_graphics_data;
struct pshine_star_graphics_data;

struct pshine_planet {
	struct pshine_celestial_body as_body;
	bool has_atmosphere;
	struct pshine_atmosphere_info atmosphere;
	struct pshine_planet_graphics_data *graphics_data;
};

struct pshine_star {
	struct pshine_celestial_body as_body;
	struct pshine_star_graphics_data *graphics_data;

};

typedef struct { int32_t x; } pshine_snorm32;
typedef struct { uint32_t x; } pshine_unorm32;
typedef struct { pshine_snorm32 x, y, z; } pshine_snorm32x3;
typedef struct { pshine_snorm32 x, y; } pshine_snorm32x2;
typedef struct { pshine_unorm32 x, y, z; } pshine_unorm32x3;
typedef struct { pshine_unorm32 x, y; } pshine_unorm32x2;

struct pshine_static_mesh_vertex {
	float position[3];
	float normal_oct[2];
	float tangent_dia;
	float texcoord[2];
};

struct pshine_planet_vertex {
	float position[3];
	float normal_oct[2];
	float tangent_dia;
};

enum pshine_vertex_type {
	PSHINE_VERTEX_STATIC_MESH,
	PSHINE_VERTEX_SKINNED_MESH,
	PSHINE_VERTEX_PLANET,
};

struct pshine_mesh_data {
	uint32_t vertex_count;
	void *vertices;
	uint32_t index_count;
	uint32_t *indices;
	enum pshine_vertex_type vertex_type;
};

/// Generate the mesh for a planet, with the
/// specified level of detail (0 - highest).
void pshine_generate_planet_mesh(
	const struct pshine_planet *planet,
	struct pshine_mesh_data *out_mesh,
	size_t lod
);

struct pshine_renderer;
struct pshine_game;

struct pshine_renderer {
	const char *name;
};

struct pshine_renderer *pshine_create_renderer();
void pshine_init_renderer(struct pshine_renderer *renderer, struct pshine_game *game);
void pshine_deinit_renderer(struct pshine_renderer *renderer);
void pshine_destroy_renderer(struct pshine_renderer *renderer);

const uint8_t *pshine_get_key_states(struct pshine_renderer *renderer);

struct pshine_game {
	struct pshine_game_data *data_own;
	size_t celestial_body_count;
	struct pshine_celestial_body **celestial_bodies_own;
	struct pshine_renderer *renderer;
	pshine_point3d camera_position;
	pshine_vector3d camera_forward;
	float camera_fov;
	float atmo_blend_factor;
	pshine_vector3d sun_position;
	float material_smoothness_;
	float time_scale;
	double time;
	bool ui_dont_render_gizmos;
	bool ui_dont_render_windows;
};

/// This is ran at the start of the game, before the renderer is initialized.
void pshine_init_game(struct pshine_game *game);
/// This is ran before the main loop but after the renderer is initialized.
void pshine_post_init_game(struct pshine_game *game);
/// This is ran at the end of the game.
void pshine_deinit_game(struct pshine_game *game);
/// This is ran each frame.
void pshine_update_game(struct pshine_game *game, float delta_time);
/// This starts the game loop.
void pshine_main_loop(struct pshine_game *game, struct pshine_renderer *renderer);

enum pshine_key {
	PSHINE_KEY_SPACE = 32,
	PSHINE_KEY_APOSTROPHE = 39, /* ' */
	PSHINE_KEY_COMMA = 44, /* , */
	PSHINE_KEY_MINUS = 45, /* - */
	PSHINE_KEY_PERIOD = 46, /* . */
	PSHINE_KEY_SLASH = 47, /* / */
	PSHINE_KEY_0 = 48,
	PSHINE_KEY_1 = 49,
	PSHINE_KEY_2 = 50,
	PSHINE_KEY_3 = 51,
	PSHINE_KEY_4 = 52,
	PSHINE_KEY_5 = 53,
	PSHINE_KEY_6 = 54,
	PSHINE_KEY_7 = 55,
	PSHINE_KEY_8 = 56,
	PSHINE_KEY_9 = 57,
	PSHINE_KEY_SEMICOLON = 59, /* ; */
	PSHINE_KEY_EQUAL = 61, /* = */
	PSHINE_KEY_A = 65,
	PSHINE_KEY_B = 66,
	PSHINE_KEY_C = 67,
	PSHINE_KEY_D = 68,
	PSHINE_KEY_E = 69,
	PSHINE_KEY_F = 70,
	PSHINE_KEY_G = 71,
	PSHINE_KEY_H = 72,
	PSHINE_KEY_I = 73,
	PSHINE_KEY_J = 74,
	PSHINE_KEY_K = 75,
	PSHINE_KEY_L = 76,
	PSHINE_KEY_M = 77,
	PSHINE_KEY_N = 78,
	PSHINE_KEY_O = 79,
	PSHINE_KEY_P = 80,
	PSHINE_KEY_Q = 81,
	PSHINE_KEY_R = 82,
	PSHINE_KEY_S = 83,
	PSHINE_KEY_T = 84,
	PSHINE_KEY_U = 85,
	PSHINE_KEY_V = 86,
	PSHINE_KEY_W = 87,
	PSHINE_KEY_X = 88,
	PSHINE_KEY_Y = 89,
	PSHINE_KEY_Z = 90,
	PSHINE_KEY_LEFT_BRACKET = 91, /* [ */
	PSHINE_KEY_BACKSLASH = 92, /* \ */
	PSHINE_KEY_RIGHT_BRACKET = 93, /* ] */
	PSHINE_KEY_GRAVE_ACCENT = 96, /* ` */
	PSHINE_KEY_WORLD_1 = 161, /* non-US #1 */
	PSHINE_KEY_WORLD_2 = 162, /* non-US #2 */
	PSHINE_KEY_ESCAPE = 256,
	PSHINE_KEY_ENTER = 257,
	PSHINE_KEY_TAB = 258,
	PSHINE_KEY_BACKSPACE = 259,
	PSHINE_KEY_INSERT = 260,
	PSHINE_KEY_DELETE = 261,
	PSHINE_KEY_RIGHT = 262,
	PSHINE_KEY_LEFT = 263,
	PSHINE_KEY_DOWN = 264,
	PSHINE_KEY_UP = 265,
	PSHINE_KEY_PAGE_UP = 266,
	PSHINE_KEY_PAGE_DOWN = 267,
	PSHINE_KEY_HOME = 268,
	PSHINE_KEY_END = 269,
	PSHINE_KEY_CAPS_LOCK = 280,
	PSHINE_KEY_SCROLL_LOCK = 281,
	PSHINE_KEY_NUM_LOCK = 282,
	PSHINE_KEY_PRINT_SCREEN = 283,
	PSHINE_KEY_PAUSE = 284,
	PSHINE_KEY_F1 = 290,
	PSHINE_KEY_F2 = 291,
	PSHINE_KEY_F3 = 292,
	PSHINE_KEY_F4 = 293,
	PSHINE_KEY_F5 = 294,
	PSHINE_KEY_F6 = 295,
	PSHINE_KEY_F7 = 296,
	PSHINE_KEY_F8 = 297,
	PSHINE_KEY_F9 = 298,
	PSHINE_KEY_F10 = 299,
	PSHINE_KEY_F11 = 300,
	PSHINE_KEY_F12 = 301,
	PSHINE_KEY_F13 = 302,
	PSHINE_KEY_F14 = 303,
	PSHINE_KEY_F15 = 304,
	PSHINE_KEY_F16 = 305,
	PSHINE_KEY_F17 = 306,
	PSHINE_KEY_F18 = 307,
	PSHINE_KEY_F19 = 308,
	PSHINE_KEY_F20 = 309,
	PSHINE_KEY_F21 = 310,
	PSHINE_KEY_F22 = 311,
	PSHINE_KEY_F23 = 312,
	PSHINE_KEY_F24 = 313,
	PSHINE_KEY_F25 = 314,
	PSHINE_KEY_KP_0 = 320,
	PSHINE_KEY_KP_1 = 321,
	PSHINE_KEY_KP_2 = 322,
	PSHINE_KEY_KP_3 = 323,
	PSHINE_KEY_KP_4 = 324,
	PSHINE_KEY_KP_5 = 325,
	PSHINE_KEY_KP_6 = 326,
	PSHINE_KEY_KP_7 = 327,
	PSHINE_KEY_KP_8 = 328,
	PSHINE_KEY_KP_9 = 329,
	PSHINE_KEY_KP_DECIMAL = 330,
	PSHINE_KEY_KP_DIVIDE = 331,
	PSHINE_KEY_KP_MULTIPLY = 332,
	PSHINE_KEY_KP_SUBTRACT = 333,
	PSHINE_KEY_KP_ADD = 334,
	PSHINE_KEY_KP_ENTER = 335,
	PSHINE_KEY_KP_EQUAL = 336,
	PSHINE_KEY_LEFT_SHIFT = 340,
	PSHINE_KEY_LEFT_CONTROL = 341,
	PSHINE_KEY_LEFT_ALT = 342,
	PSHINE_KEY_LEFT_SUPER = 343,
	PSHINE_KEY_RIGHT_SHIFT = 344,
	PSHINE_KEY_RIGHT_CONTROL = 345,
	PSHINE_KEY_RIGHT_ALT = 346,
	PSHINE_KEY_RIGHT_SUPER = 347,
	PSHINE_KEY_MENU = 348,
	PSHINE_KEY_COUNT_ = PSHINE_KEY_MENU + 1,
};

static inline bool pshine_is_key_down(struct pshine_renderer *renderer, enum pshine_key key) {
	return pshine_get_key_states(renderer)[key];
}

#endif // PSHINE_GAME_H_
