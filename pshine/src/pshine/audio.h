#ifndef PSHINE_AUDIO_H_
#define PSHINE_AUDIO_H_

/// Wrapper around MiniAudio, so that we don't have to include the giant header file everywhere.

#include <stddef.h>
#include <stdint.h>

struct pshine_audio;

struct pshine_audio *pshine_create_audio();
void pshine_destroy_audio(struct pshine_audio **au);

struct pshine_sound_info {
	const char *name;
	/// 0-1, how quiet the sound is.
	/// The reverse of volume, so that the default 0 is fully loud.
	float quiet;
	bool looping;
	/// If true, the sound won't be destroyed after it is stopped
	/// (manually or because it reached the end)
	bool dont_destroy_on_stop;
};

/// A playing sound instance.
typedef struct { size_t idx; } pshine_sound;

/// A sound group instance.
typedef struct { size_t idx; } pshine_sound_group;

pshine_sound pshine_create_sound_from_file(struct pshine_audio *au, const struct pshine_sound_info *info);
void pshine_play_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_pause_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_rewind_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_destroy_sound(struct pshine_audio *au, pshine_sound *sound);

struct pshine_sound_group_info {
	const char *name;
};

pshine_sound_group pshine_create_sound_group(struct pshine_audio *au, const struct pshine_sound_group_info *info);
void pshine_destroy_group(struct pshine_audio *au, pshine_sound_group *group);

typedef struct { size_t idx; } pshine_sound_producer;

typedef void (*pshine_sound_producer_callback)(
	uint32_t *output_frames_counts,
	float **output_frames,
	void *user_data
);

struct pshine_sound_producer_info {
	const char *name;
	pshine_sound_producer_callback callback;
	size_t output_channels;
	size_t user_data_size;
};

pshine_sound_producer pshine_create_sound_producer(
	struct pshine_audio *au, const struct pshine_sound_producer_info *info
);
void pshine_destroy_sound_producer(struct pshine_audio *au, pshine_sound_producer *producer);

pshine_sound pshine_create_produced_sound(struct pshine_audio *au, pshine_sound_producer producer);

#endif
