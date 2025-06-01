/// Wrapper around MiniAudio, so that we don't have to include the giant header file everywhere.
#include <stddef.h>

struct pshine_audio;

void pshine_init_audio(struct pshine_audio *au);
void pshine_deinit_audio(struct pshine_audio *au);

struct pshine_sound_info {
	const char *name;
	/// 0-1, how quiet the sound is.
	/// The reverse of volume, so that the default 0 is fully loud.
	float quiet;
};

/// A playing sound instance.
typedef struct { size_t idx; } pshine_sound;

/// A sound group instance.
typedef struct { size_t idx; } pshine_sound_group;

pshine_sound pshine_create_sound(struct pshine_audio *au, const struct pshine_sound_info *info);
void pshine_play_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_pause_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_rewind_sound(struct pshine_audio *au, pshine_sound sound);
void pshine_destroy_sound(struct pshine_audio *au, pshine_sound *sound);

struct pshine_sound_group_info {
	const char *name;
};

pshine_sound_group pshine_create_sound_group(struct pshine_audio *au, const struct pshine_sound_group_info *info);
void pshine_destroy_group(struct pshine_audio *au, pshine_sound_group *group);
