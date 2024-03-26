#include "GL/glcorearb.h"
#include <GL/gl.h>
#include <pshine/gfx.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PSHINE_INTERNAL_
#define PSHINE_MODNAME gfx
#include <pshine/mod.h>

#define GFX_DEBUG_SYNC 1

static void error_callback_glfw(int error, const char *msg) {
	fprintf(stderr, "GLFW error %d: %s\n", error, msg);
}

struct gfx_uniform_ {
	GLchar *own_name;
	GLuint loc;
	GLsizei count;
};

struct gfx_shader_ {
	GLuint pl;
	bool depth;
	size_t nuni;
	struct gfx_uniform_ *own_unis;
};

struct gfx_ubo_ {
	GLuint buf;
	size_t size;
	enum gfx_uniform_buffer_memory mem;
	void *ptr;
};

struct gfx_mesh_ {
	bool has_ebo;
	GLuint vao, vbo, ebo;
	size_t nvtx, nidx;
	size_t vtxsz;
	enum gfx_index_type idxty;
	size_t nattr;
	struct gfx_attr *own_attrs;
};

struct gfx_rpass_ {
	gfx_framebuffer fb;
};

enum gfx_cmd_type_ : uint8_t {
	GFX_CMD_BIND_SHADER,
	GFX_CMD_BIND_UBO,
	GFX_CMD_DRAW,
	GFX_CMD_BEGIN_RPASS,
	GFX_CMD_END_RPASS,
};

struct gfx_cmd_ {
	enum gfx_cmd_type_ type;
};

struct gfx_cmd_bind_shader_ {
	struct gfx_cmd_ cmd;
	gfx_shader shader;
};

struct gfx_cmd_draw_ {
	struct gfx_cmd_ cmd;
	gfx_mesh mesh;
};

struct gfx_cmd_bind_ubo_ {
	struct gfx_cmd_ cmd;
	gfx_uniform_buffer ubo;
	uint32_t idx;
	size_t off, sz;
};

struct gfx_cmd_begin_rpass_ {
	struct gfx_cmd_ cmd;
	gfx_render_pass rpass;
	struct gfx_color col;
	struct gfx_depth_stencil ds;
};

struct gfx_cmd_end_rpass_ {
	struct gfx_cmd_ cmd;
};

struct gfx_cmdstr_ {
	size_t ncmd;
	size_t cap, bsize;
	uint8_t *buf;
};

static size_t gfx_cmd_size_(enum gfx_cmd_type_ type) {
	switch (type) {
		case GFX_CMD_BIND_SHADER: return sizeof(struct gfx_cmd_bind_shader_);
		case GFX_CMD_BIND_UBO: return sizeof(struct gfx_cmd_bind_ubo_);
		case GFX_CMD_DRAW: return sizeof(struct gfx_cmd_draw_);
		case GFX_CMD_BEGIN_RPASS: return sizeof(struct gfx_cmd_begin_rpass_);
		case GFX_CMD_END_RPASS: return sizeof(struct gfx_cmd_end_rpass_);
		default: PSHINE_PANIC("[internal] bad command");
	}
}

static void *gfx_cmdstr_push_(struct gfx_cmdstr_ *c, enum gfx_cmd_type_ type) {
	size_t sz = gfx_cmd_size_(type);
	size_t old_cap = c->cap;
	if (c->cap == 0) c->cap = 1;
	while (c->cap < c->bsize + sz) {
		c->cap += 256;
	}
	if (old_cap == 0) c->buf = malloc(c->cap);
	else c->buf = realloc(c->buf, c->cap);
	uint8_t *ptr = c->buf + c->bsize;
	c->bsize += sz;
	++c->ncmd;
	struct gfx_cmd_ *cmd = (void*)ptr;
	cmd->type = type;
	return ptr;
}

static void gfx_cmdstr_clear_(struct gfx_cmdstr_ *c) {
	c->ncmd = 0;
	c->bsize = 0;
}

static void gfx_cmdstr_free_(struct gfx_cmdstr_ *c) {
	gfx_cmdstr_clear_(c);
	if (c->cap > 0) free(c->buf);
}

struct gfx_dyna_dead_item_ {
	size_t next;
};

struct gfx_dyna_ {
	void *ptr;
	size_t count, cap;
	size_t next_free;
};

#define GFX_DYNA_(T) union { \
	_Static_assert(sizeof(T) >= sizeof(struct gfx_dyna_dead_item_), ""); \
	struct gfx_dyna_ dyna; \
	union { \
		union { T item; struct gfx_dyna_dead_item_ dead; } *dead_ptr; \
		T *ptr; \
	}; \
}

void gfx_clear_dyna_(struct gfx_dyna_ *dyna) {
	dyna->count = 0;
	dyna->next_free = 0;
}

void gfx_free_dyna_(struct gfx_dyna_ *dyna) {
	gfx_clear_dyna_(dyna);
	if (dyna->cap > 0) free(dyna->ptr);
	dyna->cap = 0;
	dyna->ptr = NULL;
}

struct gfx_instance_ {
	char *own_name;
	void *user_ptr;
	GLFWwindow *window;
	uint32_t width, height;
	
	GFX_DYNA_(struct gfx_mesh_) meshes;
	GFX_DYNA_(struct gfx_shader_) shaders;
	GFX_DYNA_(struct gfx_ubo_) ubos;
	GFX_DYNA_(struct gfx_cmdstr_) cmds;
	GFX_DYNA_(struct gfx_rpass_) rpasses;
};

#define GFX_DYNA_ALLOC(a) (gfx_dyna_alloc_(&(a).dyna, sizeof(*(a).ptr)))
static size_t gfx_dyna_alloc_(struct gfx_dyna_ *d, size_t item_size) {
	if (d->next_free >= d->count) {
		// next_free points outside of the array, we should increase count.

		// set the next free to be outside of the array.
		d->next_free = 1 + d->count;

		size_t old_cap = d->cap; // if 0, calloc, else realloc.
	
		while (d->count >= d->cap) {
			if (d->cap == 0) {
				d->cap = 1;
			} else {
				d->cap += 256;
			}
		}

		if (old_cap == 0) {
			d->ptr = calloc(1, item_size);
		} else {
			d->ptr = realloc(
				d->ptr,
				item_size * d->cap
			);
		}

		return d->count++; // actually increase count.
	}
	// next_free points inside the array, get the dead item there
	// and set next_free to whatever it points to. like a linked list.
	// after that, the item at the old next_free is free to use.
	size_t idx = d->next_free;
	// we have to index manually because item_size is not known at compile time.
	struct gfx_dyna_dead_item_ *item
		= (void*)((uint8_t*)d->ptr + item_size * d->next_free);
	d->next_free = item->next;
	return d->next_free;
}

#define GFX_DYNA_KILL(a, i) (gfx_dyna_kill_(&(a).dyna, sizeof(*(a).ptr), (i)))
static void gfx_dyna_kill_(struct gfx_dyna_ *d, size_t item_size, size_t idx) {
	PSHINE_CHECK(d != NULL, "empty dyna");
	PSHINE_CHECK(idx < d->count, "invalid index");
	// we save next_free into the dead element and set it to point the dead element.
	// like prepending in a linked list.
	// we have to index manually because item_size is not known at compile time.
	struct gfx_dyna_dead_item_ *item
		= (void*)((uint8_t*)d->ptr + item_size * idx);
	item->next = d->next_free;
	d->next_free = idx;
}

static void gfx_gl_msg_cb(
	GLenum src,
	GLenum type,
	GLuint id,
	GLenum sev,
	GLsizei len,
	const GLchar *msg,
	const void *user_ptr
) {
	const struct gfx_instance_ *ins = user_ptr;
	const char *str_src = NULL;
	switch (src)
	{
		case GL_DEBUG_SOURCE_API: str_src = "API"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: str_src = "WINDOW SYSTEM"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: str_src = "SHADER COMPILER"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY: str_src = "THIRD PARTY"; break;
		case GL_DEBUG_SOURCE_APPLICATION: str_src = "APPLICATION"; break;
		case GL_DEBUG_SOURCE_OTHER: str_src = "OTHER"; break;
	}

	const char *str_type = NULL;
	switch (type)
	{
		case GL_DEBUG_TYPE_ERROR: str_type = "ERROR"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: str_type = "DEPRECATED BEHAVIOR"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: str_type = "UNDEFINED BEHAVIOR"; break;
		case GL_DEBUG_TYPE_PORTABILITY: str_type = "PORTABILITY"; break;
		case GL_DEBUG_TYPE_PERFORMANCE: str_type = "PERFORMANCE"; break;
		case GL_DEBUG_TYPE_MARKER: str_type = "MARKER"; break;
		case GL_DEBUG_TYPE_OTHER: str_type = "OTHER"; break;
	}

	const char *str_sev = NULL;
	switch (sev) {
		case GL_DEBUG_SEVERITY_NOTIFICATION: str_sev = "NOTIFICATION"; break;
		case GL_DEBUG_SEVERITY_LOW: str_sev = "LOW"; break;
		case GL_DEBUG_SEVERITY_MEDIUM: str_sev = "MEDIUM"; break;
		case GL_DEBUG_SEVERITY_HIGH: str_sev = "HIGH"; break;
	}
	
	fprintf(stderr, "opengl(%s) %s: %s: %s\n", str_sev, str_src, str_type, msg);
}

gfx_instance gfx_create_instance(const struct gfx_instance_info *info) {
	struct gfx_instance_ *ins = calloc(1, sizeof(*ins));
	PSHINE_CHECK(ins != NULL, "could not allocate instance");
	ins->own_name = malloc(strlen(info->name) + 1);
	strcpy(ins->own_name, info->name);
	ins->user_ptr = info->user_ptr;
	ins->width = info->width;
	ins->height = info->height;
	glfwSetErrorCallback(&error_callback_glfw);
	if (!glfwInit()) PSHINE_PANIC("could not initialize GLFW.");
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_CONTEXT_DEBUG, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	ins->window = glfwCreateWindow(ins->width, ins->height, ins->own_name, NULL, NULL);
	if (ins->window == NULL) PSHINE_PANIC("could not create window.");
	glfwMakeContextCurrent(ins->window);
	if (gl3wInit() < 0) PSHINE_PANIC("failed to initialize OpenGL.");
	glEnable(GL_DEBUG_OUTPUT);
#if defined(GFX_DEBUG_SYNC) && GFX_DEBUG_SYNC
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif // GFX_DEBUG_SYNC
	glDebugMessageCallback(&gfx_gl_msg_cb, ins);
	return (gfx_instance){ (uintptr_t)(void*)ins };
}

static void gfx_cmdstr_run_(struct gfx_instance_ *ins, struct gfx_cmdstr_ *c);

void gfx_loop(gfx_instance instance, gfx_loop_fn fn) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	
	size_t idx = GFX_DYNA_ALLOC(ins->cmds);
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[idx];
	*c = (struct gfx_cmdstr_){};

	float last_time = glfwGetTime();
	while (!glfwWindowShouldClose(ins->window)) {
		glfwPollEvents();
		float cur_time = glfwGetTime();
		float delta_time = cur_time - last_time;

		enum gfx_control_flow cf = fn(
			instance,
			(gfx_command_stream){ idx },
			delta_time,
			ins->user_ptr
		);

		if (cf == GFX_CONTROL_BREAK) break;

		gfx_cmdstr_run_(ins, c);
		gfx_cmdstr_clear_(c);

		glfwSwapBuffers(ins->window);
		last_time = cur_time;
	}

	gfx_cmdstr_free_(c);
	GFX_DYNA_KILL(ins->cmds, idx);
}

gfx_framebuffer gfx_get_output_framebuffer(gfx_instance instance) {
	return (gfx_framebuffer){ 0 };
}

gfx_uint32 gfx_get_width(gfx_instance instance) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	return ins->width;
}
gfx_uint32 gfx_get_height(gfx_instance instance) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	return ins->height;
}
gfx_bool32 gfx_is_key_down(gfx_instance instance, enum gfx_key key) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	return glfwGetKey(ins->window, (int)key);
}

void gfx_destroy_instance(gfx_instance instance) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	gfx_free_dyna_(&ins->cmds.dyna);
	gfx_free_dyna_(&ins->rpasses.dyna);
	gfx_free_dyna_(&ins->meshes.dyna);
	gfx_free_dyna_(&ins->shaders.dyna);
	gfx_free_dyna_(&ins->ubos.dyna);
	glfwDestroyWindow(ins->window);
	glfwTerminate();
	free(ins->own_name);
	free(ins);
}

static inline size_t gfx_index_size_(enum gfx_index_type e) {
	switch (e) {
		case GFX_INDEX_UINT32: return 4;
		case GFX_INDEX_UINT16: return 2;
		case GFX_INDEX_UINT8: return 1;
		default: PSHINE_PANIC("bad index type");
	}
}

static inline GLenum gfx_idx_ty_to_gl_(enum gfx_index_type e) {
	switch (e) {
		case GFX_INDEX_UINT8: return GL_UNSIGNED_BYTE;
		case GFX_INDEX_UINT16: return GL_UNSIGNED_SHORT;
		case GFX_INDEX_UINT32: return GL_UNSIGNED_INT;
		default: PSHINE_PANIC("bad inndex type");
	}
}

static inline GLenum gfx_type_to_gl_(enum gfx_type e) {
	switch (e) {
		case GFX_INT32: return GL_INT;
		case GFX_UINT32: return GL_UNSIGNED_INT;
		case GFX_INT16: return GL_SHORT;
		case GFX_UINT16: return GL_UNSIGNED_SHORT;
		case GFX_FLOAT32: return GL_FLOAT;
	}
}

static void gfx_init_mesh_(
	struct gfx_instance_ *ins,
	struct gfx_mesh_ *m,
	const void *vtxs,
	const void *idxs
) {

	glCreateBuffers(1, &m->vbo);
	glNamedBufferStorage(
		m->vbo,
		m->vtxsz * m->nvtx,
		vtxs,
		0
	);

	if (m->has_ebo) {
		glCreateBuffers(2, &m->ebo);
		glNamedBufferStorage(
			m->ebo,
			gfx_index_size_(m->idxty) * m->nidx,
			idxs,
			0
		);
	}

	glCreateVertexArrays(1, &m->vao);
	
	glVertexArrayVertexBuffer(m->vao, 0, m->vbo, 0, m->vtxsz);
	if (m->has_ebo) glVertexArrayElementBuffer(m->vao, m->ebo);

	for (size_t i = 0; i < m->nattr; ++i) {
		struct gfx_attr *a = &m->own_attrs[i];
		glEnableVertexArrayAttrib(m->vao, a->index);
		glVertexArrayAttribFormat(
			m->vao,
			a->index,
			a->count,
			gfx_type_to_gl_(a->type),
			GL_FALSE,
			a->offset
		);
		glVertexArrayAttribBinding(m->vao, a->index, 0);
	}

}

gfx_mesh gfx_create_mesh(
	gfx_instance instance,
	const struct gfx_mesh_info *info
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");

	size_t idx = GFX_DYNA_ALLOC(ins->meshes);
	struct gfx_mesh_ *m = &ins->meshes.ptr[idx];
	m->has_ebo = info->has_indices;
	m->idxty = info->index_type;
	m->nidx = info->index_count;
	m->nvtx = info->vertex_count;

	PSHINE_CHECK(
		info->vertex_count == 0 || info->vertex_data != NULL,
		"vertex_data and vertex_count mismatch."
	);

	PSHINE_CHECK(
		!info->has_indices || info->index_count == 0 || info->index_data != NULL,
		"has_indices, index_data and index_count mismatch."
	);

	m->vtxsz = info->vertex_size;

	PSHINE_CHECK(
		info->attr_count == 0 || info->attrs != NULL,
		"attrs and attr_count mismatch."
	);

	if (info->attr_count > 0) {
		m->nattr = info->attr_count;
		m->own_attrs = calloc(m->nattr, sizeof(struct gfx_attr));
		memcpy(m->own_attrs, info->attrs, m->nattr * sizeof(struct gfx_attr));
	}

	gfx_init_mesh_(ins, m, info->vertex_data, info->index_data);

	return (gfx_mesh){ idx };
}

void gfx_destroy_mesh(gfx_instance instance, gfx_mesh mesh) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(mesh.idx < ins->meshes.dyna.count, "bad mesh index (too big)");
	struct gfx_mesh_ *m = &ins->meshes.ptr[mesh.idx];
	if (m->nattr > 0) free(m->own_attrs);
	glDeleteBuffers(1, &m->vbo);
	if (m->has_ebo) glDeleteBuffers(1, &m->ebo);
	glDeleteVertexArrays(1, &m->vao);
	GFX_DYNA_KILL(ins->meshes, mesh.idx);
}

bool compile_gl_shader_(GLenum type, const char *stype, const char *src, GLuint *s) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		GLchar *log = calloc(512, sizeof(GLchar));
		GLsizei len = 0;
		glGetShaderInfoLog(shader, 512, &len, log);
		PSHINE_ERROR("failed to compile %s shader: %s", stype, log);
		free(log);
		return false;
	}
	*s = shader;
	return true;
}

gfx_shader gfx_create_shader(
	gfx_instance instance,
	const struct gfx_shader_info *info
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	size_t idx = GFX_DYNA_ALLOC(ins->shaders);
	struct gfx_shader_ *s = &ins->shaders.ptr[idx];
	s->depth = info->depth;
	GLuint vs, fs;
	bool compiled
		= compile_gl_shader_(GL_VERTEX_SHADER, "vetex", info->vertex_source, &vs)
		| compile_gl_shader_(GL_FRAGMENT_SHADER, "fragment", info->fragment_source, &fs)
		;
	PSHINE_CHECK(compiled, "failed to compile shaders");
	s->pl = glCreateProgram();
	glAttachShader(s->pl, vs);
	glAttachShader(s->pl, fs);
	glLinkProgram(s->pl);
	GLint status = GL_FALSE;
	glGetProgramiv(s->pl, GL_LINK_STATUS, &status);
	if (!status) {
		GLchar *log = calloc(512, sizeof(GLchar));
		GLsizei len = 0;
		glGetProgramInfoLog(s->pl, 512, &len, log);
		PSHINE_ERROR("failed to link program: %s", log);
		free(log);
		PSHINE_PANIC("failed to link program");
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return (gfx_shader){ idx };
}

void gfx_destroy_shader(
	gfx_instance instance,
	gfx_shader shader
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(shader.idx < ins->shaders.dyna.count, "bad shader index (too big)");
	struct gfx_shader_ *s = &ins->shaders.ptr[shader.idx];
	if (s->nuni > 0) {
		for (size_t i = 0; i < s->nuni; ++i) {
			if (s->own_unis[i].own_name != NULL)
				free(s->own_unis[i].own_name);
		}
		free(s->own_unis);
	}
	glDeleteProgram(s->pl);
	GFX_DYNA_KILL(ins->shaders, shader.idx);
}


gfx_uniform_buffer gfx_create_uniform_buffer(
	gfx_instance instance,
	const struct gfx_uniform_buffer_info *info
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	size_t idx = GFX_DYNA_ALLOC(ins->ubos);
	struct gfx_ubo_ *u = &ins->ubos.ptr[idx];
	u->size = info->size;
	u->mem = info->memory;
	glCreateBuffers(1, &u->buf);
	GLenum flags = 0;
	switch (info->memory) {
		case GFX_UNIFORM_BUFFER_READ_WRITE:
			PSHINE_PANIC("GFX_UNIFORM_BUFFER_READ_WRITE is not implemented yet");
		case GFX_UNIFORM_BUFFER_WRITE_ONLY:
			flags
				= GL_MAP_WRITE_BIT
				| GL_MAP_PERSISTENT_BIT
				| GL_MAP_COHERENT_BIT
				;
			break;
	}
	PSHINE_CHECK(flags != 0, "bad ubo info->memory");
	glNamedBufferStorage(
		u->buf,
		u->size,
		NULL,
		flags
	);
	GLenum access = 0;
	switch (info->memory) {
		case GFX_UNIFORM_BUFFER_READ_WRITE:
			PSHINE_PANIC("GFX_UNIFORM_BUFFER_READ_WRITE is not implemented yet");
		case GFX_UNIFORM_BUFFER_WRITE_ONLY:
			access
				= GL_MAP_WRITE_BIT
				| GL_MAP_PERSISTENT_BIT
				| GL_MAP_COHERENT_BIT
				| GL_MAP_INVALIDATE_BUFFER_BIT // no need; we just created the buffer.
				;
			break;
	}
	PSHINE_CHECK(access != 0, "bad ubo info->memory");
	u->ptr = glMapNamedBufferRange(u->buf, 0, u->size, access);
	return (gfx_uniform_buffer){ idx };
}

gfx_uint32 gfx_get_uniform_buffer_size(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(uniform_buffer.idx < ins->ubos.dyna.count, "bad ubo index (too big)");
	struct gfx_ubo_ *u = &ins->ubos.ptr[uniform_buffer.idx];
	return u->size;
}

void gfx_write_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer,
	void *data
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(uniform_buffer.idx < ins->ubos.dyna.count, "bad ubo index (too big)");
	struct gfx_ubo_ *u = &ins->ubos.ptr[uniform_buffer.idx];
	glInvalidateBufferData(u->buf);
	memcpy(u->ptr, data, u->size);
}

void *gfx_read_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(uniform_buffer.idx < ins->ubos.dyna.count, "bad ubo index (too big)");
	struct gfx_ubo_ *u = &ins->ubos.ptr[uniform_buffer.idx];
	PSHINE_CHECK(u->mem == GFX_UNIFORM_BUFFER_READ_WRITE, "ubo is not readable.");
	PSHINE_PANIC("GFX_UNIFORM_BUFFER_READ_WRITE is not implemented yet");
}

void gfx_destroy_uniform_buffer(
	gfx_instance instance,
	gfx_uniform_buffer uniform_buffer
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(uniform_buffer.idx < ins->ubos.dyna.count, "bad ubo index (too big)");
	struct gfx_ubo_ *u = &ins->ubos.ptr[uniform_buffer.idx];
	glUnmapNamedBuffer(u->buf);
	glDeleteBuffers(1, &u->buf);
	GFX_DYNA_KILL(ins->ubos, uniform_buffer.idx);
}

void gfx_cmd_begin_render_pass(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_render_pass render_pass,
	const struct gfx_begin_render_pass_info *info
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(command_stream.idx < ins->cmds.dyna.count, "bad cmds index (too big)");
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[command_stream.idx];
	PSHINE_CHECK(render_pass.idx < ins->rpasses.dyna.count, "bad rpass index (too big)");
	struct gfx_cmd_begin_rpass_ *m = gfx_cmdstr_push_(c, GFX_CMD_BEGIN_RPASS);
	m->rpass = render_pass;
	m->col = info->color;
	m->ds = info->depth_stencil;
}

void gfx_cmd_end_render_pass(
	gfx_instance instance,
	gfx_command_stream command_stream
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(command_stream.idx < ins->cmds.dyna.count, "bad cmds index (too big)");
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[command_stream.idx];
	gfx_cmdstr_push_(c, GFX_CMD_END_RPASS);
}

void gfx_cmd_bind_shader(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_shader shader
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(command_stream.idx < ins->cmds.dyna.count, "bad cmds index (too big)");
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[command_stream.idx];
	PSHINE_CHECK(shader.idx < ins->shaders.dyna.count, "bad shader index (too big)");
	struct gfx_cmd_bind_shader_ *m = gfx_cmdstr_push_(c, GFX_CMD_BIND_SHADER);
	m->shader = shader;
}

void gfx_cmd_draw(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_mesh mesh
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(command_stream.idx < ins->cmds.dyna.count, "bad cmds index (too big)");
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[command_stream.idx];
	PSHINE_CHECK(mesh.idx < ins->meshes.dyna.count, "bad mesh index (too big)");
	struct gfx_cmd_draw_ *m = gfx_cmdstr_push_(c, GFX_CMD_DRAW);
	m->mesh = mesh;
}

void gfx_cmd_bind_uniform_buffer(
	gfx_instance instance,
	gfx_command_stream command_stream,
	gfx_uniform_buffer uniform_buffer,
	gfx_uint32 binding
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(command_stream.idx < ins->cmds.dyna.count, "bad cmds index (too big)");
	struct gfx_cmdstr_ *c = &ins->cmds.ptr[command_stream.idx];
	PSHINE_CHECK(uniform_buffer.idx < ins->ubos.dyna.count, "bad ubo index (too big)");
	struct gfx_ubo_ *u = &ins->ubos.ptr[uniform_buffer.idx];
	struct gfx_cmd_bind_ubo_ *m = gfx_cmdstr_push_(c, GFX_CMD_BIND_UBO);
	m->ubo = uniform_buffer;
	m->idx = binding;
	m->off = 0;
	m->sz = u->size;
}

static void gfx_cmdstr_run_(struct gfx_instance_ *ins, struct gfx_cmdstr_ *c) {
	for (size_t off = 0; off < c->bsize;) {
		struct gfx_cmd_ *m = (void*)(c->buf + off);
		size_t sz = gfx_cmd_size_(m->type);
		switch (m->type) {
			case GFX_CMD_BEGIN_RPASS: {
				struct gfx_cmd_begin_rpass_ *mm = (void*)m;
				glClearColor(mm->col.r, mm->col.g, mm->col.b, mm->col.a);
				glClearDepthf(mm->ds.depth);
				glClearStencil(mm->ds.stencil);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			} break;
			case GFX_CMD_END_RPASS: {
				struct gfx_cmd_end_rpass_ *mm = (void*)m;
				(void)mm;
			} break;
			case GFX_CMD_BIND_SHADER: {
				struct gfx_cmd_bind_shader_ *mm = (void*)m;
				struct gfx_shader_ *s = &ins->shaders.ptr[mm->shader.idx];
				if (s->depth) glEnable(GL_DEPTH_TEST);
				else glDisable(GL_DEPTH_TEST);
				glUseProgram(s->pl);
			} break;
			case GFX_CMD_BIND_UBO: {
				struct gfx_cmd_bind_ubo_ *mm = (void*)m;
				struct gfx_ubo_ *u = &ins->ubos.ptr[mm->ubo.idx];
				glBindBufferRange(GL_UNIFORM_BUFFER, mm->idx, u->buf, mm->off, mm->sz);
			} break;
			case GFX_CMD_DRAW: {
				struct gfx_cmd_draw_ *mm = (void*)m;
				struct gfx_mesh_ *mesh = &ins->meshes.ptr[mm->mesh.idx];
				glBindVertexArray(mesh->vao);
				if (mesh->has_ebo) {
					GLenum idxty = gfx_idx_ty_to_gl_(mesh->idxty);
					glDrawElements(GL_TRIANGLES, mesh->nidx, idxty, (void*)0);
				} else {
					glDrawArrays(GL_TRIANGLE_STRIP, 0, mesh->nvtx);
				}
			} break;
		}
		off += sz;
	}
}

gfx_render_pass gfx_create_render_pass(
	gfx_instance instance,
	const struct gfx_render_pass_info *info
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	size_t idx = GFX_DYNA_ALLOC(ins->rpasses);
	struct gfx_rpass_ *r = &ins->rpasses.ptr[idx];
	r->fb = info->target;
	return (gfx_render_pass){ idx };
}

void gfx_destroy_render_pass(
	gfx_instance instance,
	gfx_render_pass render_pass
) {
	struct gfx_instance_ *ins = (void*)instance.ptr;
	PSHINE_CHECK(ins != NULL, "null instance");
	PSHINE_CHECK(render_pass.idx < ins->rpasses.dyna.count, "bad rpass index (too big)");
	GFX_DYNA_KILL(ins->rpasses, render_pass.idx);
}

