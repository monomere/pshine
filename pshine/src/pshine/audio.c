#include "audio.h"
#include <pshine/util.h>
#include "miniaudio.h"

struct producer_node {
	ma_node_base as_base;
	pshine_sound_producer_callback cb;
	char user[];
};

struct sound_instance {
	bool is_produced;
	union {
		ma_sound *s;
		struct producer_node *n;
	};
};

struct group_instance {
	ma_sound_group *g;
	char *name_own;
};

struct producer {
	ma_node_vtable *v;
	ma_uint32 output_channels;
	size_t user_data_size;
};

#define CHECKMA(...) __VA_ARGS__ /* TBD */

struct pshine_audio {
	// must be first so that we can get this struct from the engine pointer.
	ma_engine engine;
	PSHINE_DYNA_(struct sound_instance) sounds;
	PSHINE_DYNA_(struct group_instance) groups;
	PSHINE_DYNA_(struct producer) producers;
	PSHINE_RBUF_(ma_sound) sounds_ma;
};

void deinit_ma_sound(void *item, void *user) {
	ma_sound *s = item;
	if (s->engineNode.baseNode.vtable != nullptr) {
		ma_sound_stop(s);
		ma_sound_uninit(s);
		// Set the ma_sound structure to be uninitialized.
		s->engineNode.baseNode.vtable = nullptr;
	}
}

struct pshine_audio *pshine_create_audio() {
	struct pshine_audio *au = calloc(1, sizeof(*au));
	ma_result res = CHECKMA(ma_engine_init(nullptr, &au->engine));
	if (res != MA_SUCCESS) PSHINE_PANIC("Failed to initialize audio engine: %s", ma_result_description(res));
	au->sounds_ma.rbuf.user = au;
	au->sounds_ma.rbuf.item_deinit_fn = &deinit_ma_sound;
	PSHINE_INIT_RBUF(au->sounds_ma, 64);
	return au;
}

void pshine_destroy_audio(struct pshine_audio **au_ptr) {
	struct pshine_audio *au = *au_ptr;
	pshine_deinit_rbuf(&au->sounds_ma.rbuf);
	pshine_free_dyna_(&au->sounds.dyna);
	pshine_free_dyna_(&au->groups.dyna);
	pshine_free_dyna_(&au->producers.dyna);
	PSHINE_DEBUG("ma_engine_uninit");
	ma_engine_uninit(&au->engine);
	free(au);
	*au_ptr = nullptr;
}

void sound_destroy_on_end_cb(void *user, ma_sound *sound) {
	[[maybe_unused]] size_t idx = (size_t)(uintptr_t)user;
	deinit_ma_sound(sound, user);
}

pshine_sound pshine_create_sound_from_file(struct pshine_audio *au, const struct pshine_sound_info *info) {
	size_t idx = PSHINE_DYNA_ALLOC(au->sounds);
	ma_sound *s = au->sounds.ptr[idx].s = calloc(1, sizeof(*s)); // PSHINE_RBUF_INSERT(au->sounds_ma, &(ma_sound){});

	ma_sound_config cfg = ma_sound_config_init();
	cfg.pFilePath = info->name;
	cfg.flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC;
	cfg.isLooping = info->looping;
	cfg.pEndCallbackUserData = (void*)(uintptr_t)idx;
	if (!info->dont_destroy_on_stop) cfg.endCallback = sound_destroy_on_end_cb;
	cfg.channelsOut = 0;
	ma_result res = CHECKMA(ma_sound_init_ex(&au->engine, &cfg, s));
	if (res != MA_SUCCESS) PSHINE_ERROR("Failed to initialize sound %s: %s", info->name, ma_result_description(res));
	ma_sound_set_volume(s, 1.0f - info->quiet);

	return (pshine_sound){ idx };
}

void pshine_play_sound(struct pshine_audio *au, pshine_sound sound) {
	CHECKMA(ma_sound_start(au->sounds.ptr[sound.idx].s));
}

void pshine_pause_sound(struct pshine_audio *au, const pshine_sound sound) {
	CHECKMA(ma_sound_stop(au->sounds.ptr[sound.idx].s));
}

void pshine_rewind_sound(struct pshine_audio *au, const pshine_sound sound) {
	CHECKMA(ma_sound_seek_to_pcm_frame(au->sounds.ptr[sound.idx].s, 0));
}

void pshine_destroy_sound(struct pshine_audio *au, pshine_sound *sound) {
	if (au->sounds.ptr[sound->idx].is_produced) {
		struct producer_node *n = au->sounds.ptr[sound->idx].n;
		ma_node_uninit(n, nullptr);
		free(n);
	} else {
		ma_sound_stop(au->sounds.ptr[sound->idx].s);
		ma_sound_uninit(au->sounds.ptr[sound->idx].s);
		// Set the ma_sound structure to be uninitialized.
		au->sounds.ptr[sound->idx].s->engineNode.baseNode.vtable = nullptr;
	}
	PSHINE_DYNA_KILL(au->sounds, sound->idx);
	sound->idx = 0;
}

pshine_sound_group pshine_create_sound_group(struct pshine_audio *au, const struct pshine_sound_group_info *info) {
	size_t idx = PSHINE_DYNA_ALLOC(au->groups);
	ma_sound_group *g = au->groups.ptr[idx].g = calloc(1, sizeof(*g));
	ma_result res = CHECKMA(ma_sound_group_init(&au->engine, 0, nullptr, g));
	if (res != MA_SUCCESS) PSHINE_ERROR("Failed to initialize sound group %s: %s", info->name, ma_result_description(res));
	au->groups.ptr[idx].name_own = pshine_strdup(info->name);
	return (pshine_sound_group){ idx };
}

void pshine_destroy_group(struct pshine_audio *au, pshine_sound_group *group) {
	ma_sound_group *g = au->groups.ptr[group->idx].g;
	ma_sound_group_uninit(g);
	free(g);
	PSHINE_DYNA_KILL(au->groups, group->idx);
	group->idx = 0;
}

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
	const size_t idx = PSHINE_DYNA_ALLOC(au->producers);
	ma_node_vtable *v = calloc(1, sizeof(*v));
	au->producers.ptr[idx].v = v;
	au->producers.ptr[idx].output_channels = info->output_channels;
	au->producers.ptr[idx].user_data_size = info->user_data_size;
	v->flags = MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT;
	v->inputBusCount = 0;
	v->onGetRequiredInputFrameCount = nullptr;
	v->outputBusCount = 1;
	v->onProcess = &sound_producer_ma_callback;
	return (pshine_sound_producer){ idx };
}

void pshine_destroy_sound_producer(struct pshine_audio *au, pshine_sound_producer *producer) {
	ma_node_vtable *v = au->producers.ptr[producer->idx].v;
	free(v);
	PSHINE_DYNA_KILL(au->producers, producer->idx);
	producer->idx = 0;
}

pshine_sound pshine_create_produced_sound(struct pshine_audio *au, pshine_sound_producer producer) {
	size_t idx = PSHINE_DYNA_ALLOC(au->sounds);
	au->sounds.ptr[idx].is_produced = true;
	ma_node_graph *graph = ma_engine_get_node_graph(&au->engine);
	struct producer *p = &au->producers.ptr[producer.idx];
	struct producer_node *n = malloc(sizeof(*n) + 1);
	au->sounds.ptr[idx].n = n;
	ma_node_config cfg = ma_node_config_init();
	cfg.vtable = p->v;
	cfg.pOutputChannels = &p->output_channels;
	CHECKMA(ma_node_init(graph, &cfg, nullptr, n));
	return (pshine_sound){ idx };
}
