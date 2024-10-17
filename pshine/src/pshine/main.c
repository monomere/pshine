#include <pshine/util.h>
#include <pshine/game.h>

int pshine_argc;
const char **pshine_argv;

int main(int argc, char **argv) {
	pshine_argc = argc;
	pshine_argv = (void*)argv;
	//            ^^^^^^^
	//         C being dumb

	FILE *log_fout = fopen("log.log", "wb");
	pshine_log_sinks = (struct pshine_log_sink[]){
		(struct pshine_log_sink){ stderr, true },
		(struct pshine_log_sink){ log_fout, false },
	};
	pshine_log_sink_count = 2;

	PSHINE_INFO("started");

	struct pshine_game game = {};
	PSHINE_INFO("initializing game");
	pshine_init_game(&game);
	struct pshine_renderer *renderer = pshine_create_renderer();
	PSHINE_INFO("initializing renderer");
	pshine_init_renderer(renderer, &game);
	game.renderer = renderer;
	PSHINE_INFO("main loop");
	pshine_main_loop(&game, game.renderer);
	PSHINE_INFO("deinitializing renderer");
	pshine_deinit_renderer(renderer);
	pshine_destroy_renderer(renderer);
	PSHINE_INFO("deinitializing game");
	pshine_deinit_game(&game);

	PSHINE_INFO("ended");

	fclose(log_fout);

	return EXIT_SUCCESS;
}
