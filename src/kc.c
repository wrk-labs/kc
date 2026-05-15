/* See LICENSE file for copyright and license details. */
/* kc — kai calendar */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <time.h>

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

/* -n argument parser. Trailing unit suffix (m/h/d/w) → window in seconds;
 * bare integer → count of events; NULL → default 24h window.
 * On success returns 1, sets *window_secs (>0 = window mode) or *count
 * (>0 = count mode). Caps the window at 90 days. */
static int
parse_n_arg(const char *arg, long *window_secs, int *count)
{
	if (!arg) {
		*window_secs = 24L * 3600;
		*count = 0;
		return 1;
	}
	if (!*arg)
		return 0;

	char *end;
	long n = strtol(arg, &end, 10);
	if (n <= 0 || end == arg)
		return 0;

	if (*end == '\0') {
		/* bare integer → count */
		*window_secs = 0;
		*count = (int)n;
		return 1;
	}
	if (end[1] != '\0')
		return 0;  /* trailing junk after unit */

	long multiplier;
	switch (*end) {
	case 'm': multiplier = 60;          break;
	case 'h': multiplier = 3600;        break;
	case 'd': multiplier = 86400;       break;
	case 'w': multiplier = 7L * 86400;  break;
	default:  return 0;
	}
	long secs = n * multiplier;
	long cap = 90L * 86400;
	if (secs > cap)
		secs = cap;
	*window_secs = secs;
	*count = 0;
	return 1;
}

/* qsort comparator: events ascending by start time. */
static int
event_cmp_start(const void *a, const void *b)
{
	time_t sa = ((const struct event *)a)->start;
	time_t sb = ((const struct event *)b)->start;
	return (sa < sb) ? -1 : (sa > sb) ? 1 : 0;
}

/* Emit a summary, squashing embedded tab/newline/CR to spaces so output
 * stays one-event-per-line and tab-separated. sanitize_str() lets those
 * through, since they're benign inside the TUI. */
static void
emit_summary(const char *s)
{
	for (; *s; s++)
		putchar((*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s);
}

/* Print future events within window_secs (window mode) or up to `count`
 * (count mode). One per line: ISO timestamp + tab + summary. All-day
 * events drop the time portion. */
static void
print_upcoming(struct state *st, long window_secs, int count)
{
	qsort(st->events, st->n_events, sizeof(struct event), event_cmp_start);

	time_t now = time(NULL);
	time_t cutoff = now + window_secs;
	int printed = 0;
	int i;

	for (i = 0; i < st->n_events; i++) {
		struct event *ev = &st->events[i];
		if (ev->start < now)
			continue;
		if (window_secs > 0 && ev->start > cutoff)
			break;
		if (count > 0 && printed >= count)
			break;

		char tbuf[32];
		struct tm tm_start;
		localtime_r(&ev->start, &tm_start);
		strftime(tbuf, sizeof tbuf,
		         ev->all_day ? "%Y-%m-%d" : "%Y-%m-%dT%H:%M",
		         &tm_start);

		fputs(tbuf, stdout);
		putchar('\t');
		emit_summary(ev->summary);
		putchar('\n');
		printed++;
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: kc [-v] [-s] [-n [arg]]\n");
	fprintf(stderr, "  -v        show version\n");
	fprintf(stderr, "  -s        sync calendars\n");
	fprintf(stderr, "  -n [arg]  print upcoming events (default: next 24h)\n");
	fprintf(stderr, "            arg can be a duration (30m, 2h, 5d, 1w; cap 90d)\n");
	fprintf(stderr, "            or a bare integer count (e.g. 5)\n");
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
			kc_progress_verbose = 1;
			vdir_sync_all(&state);
			return 0;
		} else if (strcmp(argv[i], "-n") == 0) {
			const char *narg = NULL;
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				narg = argv[i + 1];
				i++;
			}
			long window_secs = 0;
			int count = 0;
			if (!parse_n_arg(narg, &window_secs, &count)) {
				fprintf(stderr, "kc: invalid -n argument '%s'\n", narg);
				return 1;
			}
			vdir_init_data_dir(home);
			vdir_load_config(home, &state);
			load_events(&state);
			print_upcoming(&state, window_secs, count);
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

	/* sync calendars on startup */
	if (state.n_calendars > 0)
		fprintf(stderr, "kc: syncing %d calendar%s...\n",
		        state.n_calendars,
		        state.n_calendars == 1 ? "" : "s");
	kc_progress_verbose = 1;
	vdir_sync_all(&state);
	kc_progress_verbose = 0;

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
