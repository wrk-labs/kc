/* See LICENSE file for copyright and license details. */
/* kc — kai calendar */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>

#include "kc.h"
#include "config.h"

static struct state state;

static void
load_events(struct state *st)
{
	int i;

	st->n_events = 0;
	for (i = 0; i < st->n_calendars; i++) {
		if (!st->calendars[i].visible)
			continue;
		st->n_events = vdir_load_calendar(
			st->calendars[i].path, i,
			st->events, MAX_EVENTS, st->n_events);
	}
	st->n_events = ical_remove_cancelled(st->events, st->n_events);
	st->need_reload = 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: kc [-v] [-s] [-c config]\n");
	fprintf(stderr, "  -v        show version\n");
	fprintf(stderr, "  -s        sync calendars\n");
	fprintf(stderr, "  -c path   config file path\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *home;
	int i;

	home = getenv("HOME");
	if (!home) {
		fprintf(stderr, "kc: $HOME not set\n");
		return 1;
	}

	setlocale(LC_ALL, "");

	memset(&state, 0, sizeof(state));
	state.running = 1;
	state.view = VIEW_MONTH;
	state.selected_event = 0;
	state.upcoming_days = 7;

	/* parse args */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0) {
			printf("kc %s\n", VERSION);
			return 0;
		} else if (strcmp(argv[i], "-s") == 0) {
			vdir_init_data_dir(home);
			vdir_load_config(home, &state);
			vdir_sync_all(&state);
			return 0;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
		} else {
			usage();
		}
	}

	/* init data dir and load config */
	vdir_init_data_dir(home);
	vdir_load_config(home, &state);

	/* set today */
	cal_today(&state.today);
	memcpy(&state.cursor, &state.today, sizeof(struct tm));

	/* load events from all calendars */
	load_events(&state);

	/* start UI */
	ui_init();

	while (state.running) {
		ui_draw(&state);
		ui_input(&state);

		if (state.need_reload)
			load_events(&state);
	}

	ui_cleanup();

	return 0;
}
