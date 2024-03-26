#ifndef PSGFX_H_
#define PSGFX_H_
#include <stddef.h>
#include <stdint.h>

typedef uint32_t gfx_bool32;
typedef uint32_t gfx_uint32;
typedef uintptr_t gfx_handle_ptr;
typedef size_t gfx_handle_idx;

#define GFX_FALSE ((gfx_bool32)0)
#define GFX_TRUE ((gfx_bool32)1)
typedef struct { gfx_handle_ptr ptr; } gfx_instance;
typedef struct { gfx_handle_idx idx; } gfx_mesh;
typedef struct { gfx_handle_idx idx; } gfx_shader;
typedef struct { gfx_handle_idx idx; } gfx_uniform_buffer;
typedef struct { gfx_handle_idx idx; } gfx_command_stream;
typedef struct { gfx_handle_idx idx; } gfx_render_pass;
typedef struct { gfx_handle_idx idx; } gfx_render_pass_instance;
typedef struct { gfx_handle_idx idx; } gfx_framebuffer;

enum gfx_index_type : gfx_uint32 {
	GFX_INDEX_NONE,
	GFX_INDEX_UINT32,
	GFX_INDEX_UINT16,
	GFX_INDEX_UINT8,
};

enum gfx_type : gfx_uint32 {
	GFX_FLOAT32,
	GFX_INT32,
	GFX_UINT32,
	GFX_INT16,
	GFX_UINT16,
};

enum gfx_control_flow : gfx_uint32 {
	GFX_CONTROL_CONTINUE,
	GFX_CONTROL_BREAK
};

struct gfx_attr {
	enum gfx_type type;
	gfx_uint32 index;
	gfx_uint32 count;
	gfx_uint32 offset;
};

typedef enum gfx_control_flow (*gfx_loop_fn)(
	gfx_instance instance,
	gfx_command_stream command_stream,
	float delta_time,
	void *user_ptr
);

struct gfx_instance_info {
	const char *name;
	gfx_uint32 width, height;
	void *user_ptr;
};

gfx_instance gfx_create_instance(const struct gfx_instance_info *info);
void gfx_loop(gfx_instance instance, gfx_loop_fn fn);
void gfx_destroy_instance(gfx_instance instance);

enum gfx_key;
gfx_bool32 gfx_is_key_down(gfx_instance instance, enum gfx_key key);

gfx_uint32 gfx_get_width(gfx_instance instance);
gfx_uint32 gfx_get_height(gfx_instance instance);

gfx_framebuffer gfx_get_output_framebuffer(gfx_instance instance);

struct gfx_mesh_info {
	const struct gfx_attr *attrs;
	gfx_uint32 attr_count;
	gfx_uint32 vertex_size;
	gfx_uint32 vertex_count;
	const void *vertex_data;
	gfx_bool32 has_indices;
	enum gfx_index_type index_type;
	gfx_uint32 index_count;
	const void *index_data;
};

gfx_mesh gfx_create_mesh(
	gfx_instance instance,
	const struct gfx_mesh_info *info
);

void gfx_destroy_mesh(gfx_instance instance, gfx_mesh mesh);

struct gfx_shader_info {
	const char *vertex_source;
	const char *fragment_source;
	gfx_bool32 depth;
};

gfx_shader gfx_create_shader(
	gfx_instance instance,
	const struct gfx_shader_info *info
);

void gfx_bind_shader(gfx_instance instance, gfx_shader shader);
void gfx_destroy_shader(gfx_instance instance, gfx_shader shader);

enum gfx_uniform_buffer_memory : gfx_uint32 {
	GFX_UNIFORM_BUFFER_READ_WRITE,
	GFX_UNIFORM_BUFFER_WRITE_ONLY,
};

struct gfx_uniform_buffer_info {
	gfx_uint32 size;
	enum gfx_uniform_buffer_memory memory;
};

gfx_uniform_buffer gfx_create_uniform_buffer(
	gfx_instance instance,
	const struct gfx_uniform_buffer_info *info
);

gfx_uint32 gfx_get_uniform_buffer_size(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
);

void gfx_write_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer,
	void *data
);

void *gfx_read_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
);

void gfx_destroy_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
);

struct gfx_render_pass_info {
	gfx_framebuffer target;
};

gfx_render_pass gfx_create_render_pass(
	gfx_instance instance,
	const struct gfx_render_pass_info *info
);

void gfx_destroy_render_pass(
	gfx_instance instance,
	gfx_render_pass render_pass
);

struct gfx_color {
	float r, g, b, a;
};

struct gfx_depth_stencil {
	float depth;
	gfx_uint32 stencil;
};

struct gfx_begin_render_pass_info {
	struct gfx_color color;
	struct gfx_depth_stencil depth_stencil;
};

void gfx_cmd_begin_render_pass(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_render_pass render_pass,
	const struct gfx_begin_render_pass_info *info
);

void gfx_cmd_end_render_pass(
	gfx_instance instance,
	gfx_command_stream command_stream
);

void gfx_cmd_bind_shader(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_shader shader
);

void gfx_cmd_bind_uniform_buffer(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_uniform_buffer uniform_buffer,
	gfx_uint32 binding
);

void gfx_cmd_draw(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_mesh mesh
);

enum gfx_key {
	GFX_KEY_SPACE = 32,
	GFX_KEY_APOSTROPHE = 39, /* ' */
	GFX_KEY_COMMA = 44, /* , */
	GFX_KEY_MINUS = 45, /* - */
	GFX_KEY_PERIOD = 46, /* . */
	GFX_KEY_SLASH = 47, /* / */
	GFX_KEY_0 = 48,
	GFX_KEY_1 = 49,
	GFX_KEY_2 = 50,
	GFX_KEY_3 = 51,
	GFX_KEY_4 = 52,
	GFX_KEY_5 = 53,
	GFX_KEY_6 = 54,
	GFX_KEY_7 = 55,
	GFX_KEY_8 = 56,
	GFX_KEY_9 = 57,
	GFX_KEY_SEMICOLON = 59, /* ; */
	GFX_KEY_EQUAL = 61, /* = */
	GFX_KEY_A = 65,
	GFX_KEY_B = 66,
	GFX_KEY_C = 67,
	GFX_KEY_D = 68,
	GFX_KEY_E = 69,
	GFX_KEY_F = 70,
	GFX_KEY_G = 71,
	GFX_KEY_H = 72,
	GFX_KEY_I = 73,
	GFX_KEY_J = 74,
	GFX_KEY_K = 75,
	GFX_KEY_L = 76,
	GFX_KEY_M = 77,
	GFX_KEY_N = 78,
	GFX_KEY_O = 79,
	GFX_KEY_P = 80,
	GFX_KEY_Q = 81,
	GFX_KEY_R = 82,
	GFX_KEY_S = 83,
	GFX_KEY_T = 84,
	GFX_KEY_U = 85,
	GFX_KEY_V = 86,
	GFX_KEY_W = 87,
	GFX_KEY_X = 88,
	GFX_KEY_Y = 89,
	GFX_KEY_Z = 90,
	GFX_KEY_LEFT_BRACKET = 91, /* [ */
	GFX_KEY_BACKSLASH = 92, /* \ */
	GFX_KEY_RIGHT_BRACKET = 93, /* ] */
	GFX_KEY_GRAVE_ACCENT = 96, /* ` */
	GFX_KEY_WORLD_1 = 161, /* non-US #1 */
	GFX_KEY_WORLD_2 = 162, /* non-US #2 */
	GFX_KEY_ESCAPE = 256,
	GFX_KEY_ENTER = 257,
	GFX_KEY_TAB = 258,
	GFX_KEY_BACKSPACE = 259,
	GFX_KEY_INSERT = 260,
	GFX_KEY_DELETE = 261,
	GFX_KEY_RIGHT = 262,
	GFX_KEY_LEFT = 263,
	GFX_KEY_DOWN = 264,
	GFX_KEY_UP = 265,
	GFX_KEY_PAGE_UP = 266,
	GFX_KEY_PAGE_DOWN = 267,
	GFX_KEY_HOME = 268,
	GFX_KEY_END = 269,
	GFX_KEY_CAPS_LOCK = 280,
	GFX_KEY_SCROLL_LOCK = 281,
	GFX_KEY_NUM_LOCK = 282,
	GFX_KEY_PRINT_SCREEN = 283,
	GFX_KEY_PAUSE = 284,
	GFX_KEY_F1 = 290,
	GFX_KEY_F2 = 291,
	GFX_KEY_F3 = 292,
	GFX_KEY_F4 = 293,
	GFX_KEY_F5 = 294,
	GFX_KEY_F6 = 295,
	GFX_KEY_F7 = 296,
	GFX_KEY_F8 = 297,
	GFX_KEY_F9 = 298,
	GFX_KEY_F10 = 299,
	GFX_KEY_F11 = 300,
	GFX_KEY_F12 = 301,
	GFX_KEY_F13 = 302,
	GFX_KEY_F14 = 303,
	GFX_KEY_F15 = 304,
	GFX_KEY_F16 = 305,
	GFX_KEY_F17 = 306,
	GFX_KEY_F18 = 307,
	GFX_KEY_F19 = 308,
	GFX_KEY_F20 = 309,
	GFX_KEY_F21 = 310,
	GFX_KEY_F22 = 311,
	GFX_KEY_F23 = 312,
	GFX_KEY_F24 = 313,
	GFX_KEY_F25 = 314,
	GFX_KEY_KP_0 = 320,
	GFX_KEY_KP_1 = 321,
	GFX_KEY_KP_2 = 322,
	GFX_KEY_KP_3 = 323,
	GFX_KEY_KP_4 = 324,
	GFX_KEY_KP_5 = 325,
	GFX_KEY_KP_6 = 326,
	GFX_KEY_KP_7 = 327,
	GFX_KEY_KP_8 = 328,
	GFX_KEY_KP_9 = 329,
	GFX_KEY_KP_DECIMAL = 330,
	GFX_KEY_KP_DIVIDE = 331,
	GFX_KEY_KP_MULTIPLY = 332,
	GFX_KEY_KP_SUBTRACT = 333,
	GFX_KEY_KP_ADD = 334,
	GFX_KEY_KP_ENTER = 335,
	GFX_KEY_KP_EQUAL = 336,
	GFX_KEY_LEFT_SHIFT = 340,
	GFX_KEY_LEFT_CONTROL = 341,
	GFX_KEY_LEFT_ALT = 342,
	GFX_KEY_LEFT_SUPER = 343,
	GFX_KEY_RIGHT_SHIFT = 344,
	GFX_KEY_RIGHT_CONTROL = 345,
	GFX_KEY_RIGHT_ALT = 346,
	GFX_KEY_RIGHT_SUPER = 347,
	GFX_KEY_MENU = 348,
	GFX_KEY_LAST = GFX_KEY_MENU,
};

#endif // PSGFX_H_

