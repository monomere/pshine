/// Wrapper around MiniAudio, so that we don't include the giant header file.

struct pshine_audio;

void pshine_init_audio(struct pshine_audio *au);
void pshine_deinit_audio(struct pshine_audio *au);

struct pshine_sound_schedule {
	const char *sound_name;
	/// 0-1, how quiet the sound is.
	/// The reverse of volume, so that the default 0 is fully loud.
	float quiet;
};

void pshine_schedule_sound(struct pshine_audio *au, const struct pshine_sound_schedule *schedule);
