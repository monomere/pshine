#include "audio.h"
#include <pshine/util.h>
#include "miniaudio.h"

struct sound_instance {
	ma_sound *s;
};

struct group_instance {
	ma_sound_group *g;
	char *name_own;
};

struct producer {
	ma_node_vtable *v;
};

#define CHECKMA(...) __VA_ARGS__ /* TBD */

struct pshine_audio {
	ma_engine *engine;
	PSHINE_DYNA_(struct sound_instance) sounds;
	PSHINE_DYNA_(struct group_instance) groups;
	PSHINE_DYNA_(struct producer) producers;
};

void pshine_init_audio(struct pshine_audio *au) {
	au->engine = calloc(1, sizeof(*au->engine));
	ma_result res;
	res = CHECKMA(ma_engine_init(nullptr, au->engine));
	if (res != MA_SUCCESS) PSHINE_PANIC("Failed to initialize audio engine: %s", ma_result_description(res));
}

void pshine_deinit_audio(struct pshine_audio *au) {
	ma_engine_uninit(au->engine);
	free(au->engine);
	au->engine = nullptr;
}

pshine_sound pshine_create_sound(struct pshine_audio *au, const struct pshine_sound_info *info) {
	size_t idx = PSHINE_DYNA_ALLOC(au->sounds);
	ma_sound *s = au->sounds.ptr[idx].s = calloc(1, sizeof(*s));
	ma_result res = CHECKMA(ma_sound_init_from_file(
		au->engine,
		info->name,
		MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC,
		nullptr,
		nullptr,
		s
	));
	if (res != MA_SUCCESS) PSHINE_ERROR("Failed to initialize sound %s: %s", info->name, ma_result_description(res));
	ma_sound_set_volume(s, 1.0f - info->quiet);
	return (pshine_sound){ idx };
}

void pshine_play_sound(struct pshine_audio *au, pshine_sound sound) {
	CHECKMA(ma_sound_start(au->sounds.ptr[sound.idx].s));
}

void pshine_pause_sound(struct pshine_audio *au, pshine_sound sound) {
	CHECKMA(ma_sound_stop(au->sounds.ptr[sound.idx].s));
}

void pshine_rewind_sound(struct pshine_audio *au, pshine_sound sound) {
	CHECKMA(ma_sound_seek_to_pcm_frame(au->sounds.ptr[sound.idx].s, 0));
}

void pshine_destroy_sound(struct pshine_audio *au, pshine_sound *sound) {
	ma_sound *s = au->sounds.ptr[sound->idx].s;
	ma_sound_uninit(s);
	sound->idx = 0;
}

pshine_sound_group pshine_create_sound_group(struct pshine_audio *au, const struct pshine_sound_group_info *info) {
	size_t idx = PSHINE_DYNA_ALLOC(au->groups);
	ma_sound_group *g = au->groups.ptr[idx].g = calloc(1, sizeof(*g));
	ma_result res = CHECKMA(ma_sound_group_init(au->engine, 0, nullptr, g));
	if (res != MA_SUCCESS) PSHINE_ERROR("Failed to initialize sound group %s: %s", info->name, ma_result_description(res));
	au->groups.ptr[idx].name_own = pshine_strdup(info->name);
	return (pshine_sound_group){ idx };
}

void pshine_destroy_group(struct pshine_audio *au, pshine_sound_group *group) {
	ma_sound_group *g = au->groups.ptr[group->idx].g;
	ma_sound_group_uninit(g);
	free(g);
	group->idx = 0;
}

struct producer_node {
	ma_node_base as_base;
	pshine_sound_producer_callback cb;
	char user[];
};

static void sound_producer_ma_callback(
	ma_node* node,
	const float **ppFramesIn,
	ma_uint32 *pFrameCountIn,
	float **ppFramesOut,
	ma_uint32 *pFrameCountOut
) {
	struct producer_node *n = (void *)node;
	n->cb(pFrameCountOut, ppFramesOut, (void*)n->user);
}

pshine_sound_producer pshine_create_sound_producer(
	struct pshine_audio *au, const struct pshine_sound_producer_info *info
) {
	size_t idx = PSHINE_DYNA_ALLOC(au->producers);
	ma_node_vtable *v = au->producers.ptr[idx].v = calloc(1, sizeof(*v));
	v->flags = MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT;
	v->inputBusCount = 0;
	v->onGetRequiredInputFrameCount = nullptr;
	v->outputBusCount = info->output_channels;
	v->onProcess = &sound_producer_ma_callback;
	return (pshine_sound_producer){ idx };
}

void pshine_destroy_sound_producer(struct pshine_audio *au, pshine_sound_producer *producer) {
	ma_node_vtable *v = au->producers.ptr[producer->idx].v;
	free(v);
	producer->idx = 0;
}
