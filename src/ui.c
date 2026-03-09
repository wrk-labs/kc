/* See LICENSE file for copyright and license details. */
/* ui.c — ncurses interface for kc */

#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>

#include "kc.h"
#include "config.h"

/* layout constants */
#define SIDEBAR_W     18
#define CAL_PANEL_W   25
#define MIN_EVENT_W   40
#define HEADER_H       3
#define STATUS_H       1
#define DAY_NAMES_H    1

/* windows */
static WINDOW *win_sidebar; /* calendar list (far left) */
static WINDOW *win_events;  /* event list (center-top) */
static WINDOW *win_detail;  /* event detail (center-bottom) */
static WINDOW *win_cal;     /* month calendar grid (right) */
static WINDOW *win_status;  /* bottom status bar */
static WINDOW *win_header;  /* top header */

/*
 * Kai color palette — matches st config.h
 * ncurses init_color() takes 0-1000 scale values.
 * Hex-to-1000: component * 1000 / 255
 */
#define KAI_BLACK       16  /* #161616 */
#define KAI_RED         17  /* #e85555 */
#define KAI_GREEN       18  /* #6ab050 */
#define KAI_ORANGE      19  /* #e38735 */
#define KAI_BLUE        20  /* #5c8dcc */
#define KAI_MAGENTA     21  /* #9664dc */
#define KAI_CYAN        22  /* #50beb4 */
#define KAI_WHITE       23  /* #c8c8c8 */
#define KAI_DIM         24  /* #555555 */
#define KAI_BRIGHTWHITE 25  /* #eeeeee */
#define KAI_HIGHLIGHT   26  /* #2a2a2a */
#define KAI_BORDER      27  /* #333333 */

static void
init_colors(void)
{
	start_color();
	use_default_colors();

	if (can_change_color() && COLORS >= 28) {
		/* define Kai palette using exact hex values */
		init_color(KAI_BLACK,        86,   86,   86);   /* #161616 */
		init_color(KAI_RED,         910,  333,  333);   /* #e85555 */
		init_color(KAI_GREEN,       416,  690,  314);   /* #6ab050 */
		init_color(KAI_ORANGE,      890,  529,  208);   /* #e38735 */
		init_color(KAI_BLUE,        361,  553,  800);   /* #5c8dcc */
		init_color(KAI_MAGENTA,     588,  392,  863);   /* #9664dc */
		init_color(KAI_CYAN,        314,  745,  706);   /* #50beb4 */
		init_color(KAI_WHITE,       784,  784,  784);   /* #c8c8c8 */
		init_color(KAI_DIM,         333,  333,  333);   /* #555555 */
		init_color(KAI_BRIGHTWHITE, 933,  933,  933);   /* #eeeeee */
		init_color(KAI_HIGHLIGHT,   165,  165,  165);   /* #2a2a2a */
		init_color(KAI_BORDER,      200,  200,  200);   /* #333333 */

		/* base pairs */
		init_pair(COL_FG,          KAI_WHITE,       -1);
		init_pair(COL_ACCENT,      KAI_ORANGE,      -1);
		init_pair(COL_DIM,         KAI_DIM,         -1);
		init_pair(COL_TODAY,       KAI_ORANGE,       KAI_HIGHLIGHT);
		init_pair(COL_SELECTED,    KAI_BLACK,        KAI_ORANGE);
		init_pair(COL_BORDER,      KAI_WHITE,       -1);
		init_pair(COL_ACCEPTED,    KAI_GREEN,        -1);
		init_pair(COL_DECLINED,    KAI_RED,          -1);
		init_pair(COL_TENTATIVE,   KAI_BLUE,         -1);
		init_pair(COL_NEEDSACTION, KAI_MAGENTA,      -1);

		/* calendar-specific color pairs (offset by 32) */
		init_pair(32, KAI_ORANGE,   -1);   /* cal 1 */
		init_pair(33, KAI_BLUE,     -1);   /* cal 2 */
		init_pair(34, KAI_MAGENTA,  -1);   /* cal 3 */
		init_pair(35, KAI_GREEN,    -1);   /* cal 4 */
		init_pair(36, KAI_CYAN,     -1);   /* cal 5 */
		init_pair(37, KAI_RED,      -1);   /* cal 6 */
		init_pair(38, KAI_WHITE,    -1);   /* cal 7 */
		init_pair(39, KAI_BRIGHTWHITE, -1);/* cal 8 */
	} else {
		/* fallback for terminals that can't redefine colors */
		init_pair(COL_FG,          COLOR_WHITE,   -1);
		init_pair(COL_ACCENT,      COLOR_YELLOW,  -1);
		init_pair(COL_DIM,         8,             -1);
		init_pair(COL_TODAY,       COLOR_YELLOW,  8);
		init_pair(COL_SELECTED,    COLOR_BLACK,   COLOR_YELLOW);
		init_pair(COL_BORDER,      COLOR_WHITE,   -1);
		init_pair(COL_ACCEPTED,    COLOR_GREEN,   -1);
		init_pair(COL_DECLINED,    COLOR_RED,     -1);
		init_pair(COL_TENTATIVE,   COLOR_BLUE,    -1);
		init_pair(COL_NEEDSACTION, COLOR_MAGENTA, -1);

		init_pair(32, COLOR_YELLOW,  -1);
		init_pair(33, COLOR_BLUE,    -1);
		init_pair(34, COLOR_MAGENTA, -1);
		init_pair(35, COLOR_GREEN,   -1);
		init_pair(36, COLOR_CYAN,    -1);
		init_pair(37, COLOR_RED,     -1);
		init_pair(38, COLOR_WHITE,   -1);
		init_pair(39, COLOR_YELLOW,  -1);
	}
}

void
ui_init(void)
{
	set_escdelay(25);
	initscr();
	raw();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	mouseinterval(0);
	timeout(100); /* 100ms for responsive UI */

	init_colors();

	/* create windows: sidebar | events/detail | calendar grid */
	{
		int mid_w = COLS - SIDEBAR_W - CAL_PANEL_W;
		int body_h = LINES - HEADER_H - STATUS_H;
		int ev_h = body_h / 2;
		int det_h = body_h - ev_h;

		win_header  = newwin(HEADER_H, COLS, 0, 0);
		win_sidebar = newwin(body_h, SIDEBAR_W, HEADER_H, 0);
		win_events  = newwin(ev_h, mid_w, HEADER_H, SIDEBAR_W);
		win_detail  = newwin(det_h, mid_w, HEADER_H + ev_h, SIDEBAR_W);
		win_cal     = newwin(body_h, CAL_PANEL_W, HEADER_H, COLS - CAL_PANEL_W);
		win_status  = newwin(STATUS_H, COLS, LINES - STATUS_H, 0);
	}
}

void
ui_cleanup(void)
{
	delwin(win_header);
	delwin(win_sidebar);
	delwin(win_cal);
	delwin(win_events);
	delwin(win_detail);
	delwin(win_status);
	endwin();
}

/* count events on a given day */
static int
count_day_events(const struct state *st, int year, int month, int day)
{
	int i, count = 0;
	struct tm eday;

	for (i = 0; i < st->n_events; i++) {
		if (!st->calendars[st->events[i].cal_idx].visible)
			continue;
		localtime_r(&st->events[i].start, &eday);
		if (eday.tm_year == year - 1900 &&
		    eday.tm_mon == month - 1 &&
		    eday.tm_mday == day)
			count++;
	}
	return count;
}

/* get events for cursor day, sorted by start time */
static int
get_day_events(const struct state *st, const struct event **out, int max)
{
	int i, count = 0;
	struct tm eday;

	for (i = 0; i < st->n_events && count < max; i++) {
		if (!st->calendars[st->events[i].cal_idx].visible)
			continue;
		localtime_r(&st->events[i].start, &eday);
		if (cal_same_day(&eday, &st->cursor))
			out[count++] = &st->events[i];
	}

	/* simple insertion sort by start time */
	for (i = 1; i < count; i++) {
		const struct event *tmp = out[i];
		int j = i - 1;
		while (j >= 0 && out[j]->start > tmp->start) {
			out[j + 1] = out[j];
			j--;
		}
		out[j + 1] = tmp;
	}

	return count;
}

static void
draw_header(const struct state *st __attribute__((unused)))
{
	char buf[32];
	int len, x;

	werase(win_header);

	/* kc branding — centered on middle row */
	snprintf(buf, sizeof(buf), "Kai Calendar v%s", VERSION);
	len = (int)strlen(buf);
	x = (COLS - len) / 2;
	if (x < 0) x = 0;

	wattron(win_header, A_BOLD | COLOR_PAIR(COL_ACCENT));
	mvwprintw(win_header, 1, x, "Kai Calendar");
	wattroff(win_header, A_BOLD | COLOR_PAIR(COL_ACCENT));
	wattron(win_header, COLOR_PAIR(COL_DIM));
	wprintw(win_header, " v%s", VERSION);
	wattroff(win_header, COLOR_PAIR(COL_DIM));

	/* separator line */
	wattron(win_header, COLOR_PAIR(COL_BORDER));
	mvwhline(win_header, 2, 0, ACS_HLINE, COLS);
	wattroff(win_header, COLOR_PAIR(COL_BORDER));

	wrefresh(win_header);
}

static void
draw_sidebar(const struct state *st)
{
	int i, row = 0;
	int max_w = getmaxx(win_sidebar);

	werase(win_sidebar);

	for (i = 0; i < st->n_calendars; i++) {
		int cpair = 32 + i;

		if (st->calendars[i].visible) {
			wattron(win_sidebar, COLOR_PAIR(cpair));
			mvwprintw(win_sidebar, row, 1, "●");
			wattroff(win_sidebar, COLOR_PAIR(cpair));
		} else {
			wattron(win_sidebar, COLOR_PAIR(COL_DIM));
			mvwprintw(win_sidebar, row, 1, "○");
			wattroff(win_sidebar, COLOR_PAIR(COL_DIM));
		}

		if (st->calendars[i].visible)
			wattron(win_sidebar, COLOR_PAIR(COL_FG));
		else
			wattron(win_sidebar, COLOR_PAIR(COL_DIM));

		mvwprintw(win_sidebar, row, 3, "%.*s", max_w - 4,
		          st->calendars[i].name);

		if (st->calendars[i].visible)
			wattroff(win_sidebar, COLOR_PAIR(COL_FG));
		else
			wattroff(win_sidebar, COLOR_PAIR(COL_DIM));

		row++;
	}

	/* vertical separator on right edge */
	{
		int h = getmaxy(win_sidebar);
		wattron(win_sidebar, COLOR_PAIR(COL_BORDER));
		for (i = 0; i < h; i++)
			mvwaddch(win_sidebar, i, max_w - 1, ACS_VLINE);
		wattroff(win_sidebar, COLOR_PAIR(COL_BORDER));
	}

	wrefresh(win_sidebar);
}

static void
draw_month_cal(const struct state *st)
{
	int year = st->cursor.tm_year + 1900;
	int month = st->cursor.tm_mon + 1;
	int days = cal_days_in_month(year, month);
	int start_dow = cal_dow(year, month, 1);
	int row, col, day;
	const char *dow_labels;
	char month_buf[32];

	werase(win_cal);

	/* month + year title */
	strftime(month_buf, sizeof(month_buf), "%B %Y", &st->cursor);
	wattron(win_cal, A_BOLD | COLOR_PAIR(COL_ACCENT));
	mvwprintw(win_cal, 0, 2, "%s", month_buf);
	wattroff(win_cal, A_BOLD | COLOR_PAIR(COL_ACCENT));

	/* day-of-week header */
	if (first_dow == 1)
		dow_labels = "Mo Tu We Th Fr Sa Su";
	else
		dow_labels = "Su Mo Tu We Th Fr Sa";

	wattron(win_cal, COLOR_PAIR(COL_DIM));
	mvwprintw(win_cal, 1, 2, "%s", dow_labels);
	wattroff(win_cal, COLOR_PAIR(COL_DIM));

	/* adjust start for first_dow */
	int offset = start_dow - first_dow;
	if (offset < 0)
		offset += 7;

	/* draw days */
	day = 1;
	for (row = 0; row < 6 && day <= days; row++) {
		for (col = 0; col < 7 && day <= days; col++) {
			if (row == 0 && col < offset)
				continue;

			int x = 2 + col * 3;
			int y = 2 + row;
			int is_cursor = (day == st->cursor.tm_mday);
			int is_today = (st->today.tm_year == st->cursor.tm_year &&
			                st->today.tm_mon == st->cursor.tm_mon &&
			                day == st->today.tm_mday);
			int has_events = count_day_events(st, year, month, day) > 0;

			if (is_cursor) {
				wattron(win_cal, COLOR_PAIR(COL_SELECTED) | A_BOLD);
			} else if (is_today) {
				wattron(win_cal, COLOR_PAIR(COL_TODAY) | A_BOLD);
			} else if (has_events) {
				wattron(win_cal, COLOR_PAIR(COL_ACCENT));
			}

			mvwprintw(win_cal, y, x, "%2d", day);

			if (is_cursor) {
				wattroff(win_cal, COLOR_PAIR(COL_SELECTED) | A_BOLD);
			} else if (is_today) {
				wattroff(win_cal, COLOR_PAIR(COL_TODAY) | A_BOLD);
			} else if (has_events) {
				wattroff(win_cal, COLOR_PAIR(COL_ACCENT));
			}

			day++;
		}
	}

	/* --- upcoming events below the grid --- */
	{
		int cal_h = getmaxy(win_cal);
		int cal_w = getmaxx(win_cal);
		int urow = 2 + row + 1; /* below last grid row + gap */
		time_t now = time(NULL);
		time_t cutoff = 0;
		const struct event *upcoming[MAX_EVENTS];
		int nu = 0, i, j;
		char title_buf[48];

		/* compute cutoff time based on upcoming_days */
		if (st->upcoming_days > 0)
			cutoff = now + (time_t)st->upcoming_days * 86400;

		/* separator */
		if (urow < cal_h - 1) {
			wattron(win_cal, COLOR_PAIR(COL_BORDER));
			mvwhline(win_cal, urow, 1, ACS_HLINE, cal_w - 1);
			wattroff(win_cal, COLOR_PAIR(COL_BORDER));
			urow++;
		}

		/* title with day count */
		if (urow < cal_h) {
			if (st->upcoming_days > 0)
				snprintf(title_buf, sizeof(title_buf),
				         "Upcoming (%d days)", st->upcoming_days);
			else
				snprintf(title_buf, sizeof(title_buf), "Upcoming");
			wattron(win_cal, A_BOLD | COLOR_PAIR(COL_ACCENT));
			mvwprintw(win_cal, urow, 2, "%s", title_buf);
			wattroff(win_cal, A_BOLD | COLOR_PAIR(COL_ACCENT));
			urow++;
		}

		/* collect future events from visible calendars */
		for (i = 0; i < st->n_events; i++) {
			if (st->events[i].cal_idx < 0 ||
			    st->events[i].cal_idx >= st->n_calendars)
				continue;
			if (!st->calendars[st->events[i].cal_idx].visible)
				continue;
			if (st->events[i].end <= now)
				continue;
			if (cutoff > 0 && st->events[i].start >= cutoff)
				continue;
			upcoming[nu++] = &st->events[i];
		}

		/* sort by start time (insertion sort) */
		for (i = 1; i < nu; i++) {
			const struct event *tmp = upcoming[i];
			j = i - 1;
			while (j >= 0 && upcoming[j]->start > tmp->start) {
				upcoming[j + 1] = upcoming[j];
				j--;
			}
			upcoming[j + 1] = tmp;
		}

		/* display upcoming events, clustered by day */
		{
			int prev_yday = -1, prev_year = -1;
			for (i = 0; i < nu && urow < cal_h; i++) {
				const struct event *ev = upcoming[i];
				struct tm et;
				char time_buf[8];
				int cpair;

				localtime_r(&ev->start, &et);

				/* day header when date changes */
				if (et.tm_yday != prev_yday || et.tm_year != prev_year) {
					char day_hdr[32];
					struct tm today_tm;
					time_t today_t = now;
					localtime_r(&today_t, &today_tm);

					if (et.tm_yday == today_tm.tm_yday &&
					    et.tm_year == today_tm.tm_year)
						snprintf(day_hdr, sizeof(day_hdr), "Today");
					else if (et.tm_yday == today_tm.tm_yday + 1 &&
					         et.tm_year == today_tm.tm_year)
						snprintf(day_hdr, sizeof(day_hdr), "Tomorrow");
					else
						strftime(day_hdr, sizeof(day_hdr), "%a, %b %d", &et);

					if (prev_yday >= 0)
						urow++; /* blank line between day groups */
					if (urow >= cal_h) break;

					wattron(win_cal, A_BOLD | COLOR_PAIR(COL_DIM));
					mvwprintw(win_cal, urow, 2, "%s", day_hdr);
					wattroff(win_cal, A_BOLD | COLOR_PAIR(COL_DIM));
					urow++;

					prev_yday = et.tm_yday;
					prev_year = et.tm_year;
				}

				if (urow >= cal_h) break;

				/* time + summary on one line */
				cpair = 32 + ev->cal_idx;
				if (!ev->all_day) {
					strftime(time_buf, sizeof(time_buf), "%H:%M", &et);
					wattron(win_cal, COLOR_PAIR(COL_DIM));
					mvwprintw(win_cal, urow, 4, "%s", time_buf);
					wattroff(win_cal, COLOR_PAIR(COL_DIM));
					wattron(win_cal, COLOR_PAIR(cpair));
					wprintw(win_cal, " %.*s", cal_w - 12, ev->summary);
					wattroff(win_cal, COLOR_PAIR(cpair));
				} else {
					wattron(win_cal, COLOR_PAIR(cpair));
					mvwprintw(win_cal, urow, 4, "%.*s",
					          cal_w - 5, ev->summary);
					wattroff(win_cal, COLOR_PAIR(cpair));
				}
				urow++;
			}
		}

		if (nu == 0 && urow < cal_h) {
			wattron(win_cal, COLOR_PAIR(COL_DIM));
			mvwprintw(win_cal, urow, 2, "no upcoming events");
			wattroff(win_cal, COLOR_PAIR(COL_DIM));
		}
	}

	/* vertical separator on left edge */
	{
		int h = getmaxy(win_cal);
		wattron(win_cal, COLOR_PAIR(COL_BORDER));
		for (row = 0; row < h; row++)
			mvwaddch(win_cal, row, 0, ACS_VLINE);
		wattroff(win_cal, COLOR_PAIR(COL_BORDER));
	}

	wrefresh(win_cal);
}

/* find the user's partstat in an event, returns -1 if not an attendee */
static int
user_partstat(const struct state *st, const struct event *ev)
{
	int i;
	const char *emails[3];
	int n_emails = 0;

	/* collect all possible user emails to match against */
	if (ev->cal_idx >= 0 && ev->cal_idx < st->n_calendars &&
	    st->calendars[ev->cal_idx].email[0])
		emails[n_emails++] = st->calendars[ev->cal_idx].email;
	if (st->user_email[0])
		emails[n_emails++] = st->user_email;
	/* also try organizer email — user may be organizer with a
	 * different email than configured (common with iCloud) */
	if (ev->organizer_email[0])
		emails[n_emails++] = ev->organizer_email;

	if (n_emails == 0)
		return -1;

	for (i = 0; i < ev->n_attendees; i++) {
		int j;
		for (j = 0; j < n_emails; j++) {
			if (strcasecmp(ev->attendees[i].email, emails[j]) == 0)
				return ev->attendees[i].status;
		}
	}
	return -1;
}

/* get the effective email for a calendar (per-cal or global fallback) */
static const char *
cal_email(const struct state *st, int cal_idx)
{
	if (cal_idx >= 0 && cal_idx < st->n_calendars &&
	    st->calendars[cal_idx].email[0])
		return st->calendars[cal_idx].email;
	return st->user_email;
}

static void
draw_events(const struct state *st)
{
	const struct event *day_events[256];
	int n, i;
	char tbuf[32];
	char datebuf[64];
	struct tm t;
	int max_h = getmaxy(win_events);
	int max_w = getmaxx(win_events);

	werase(win_events);

	/* date header */
	strftime(datebuf, sizeof(datebuf), date_fmt, &st->cursor);
	wattron(win_events, A_BOLD | COLOR_PAIR(COL_FG));
	mvwprintw(win_events, 0, 1, "%s", datebuf);
	wattroff(win_events, A_BOLD | COLOR_PAIR(COL_FG));

	n = get_day_events(st, day_events, 256);

	if (n == 0) {
		wattron(win_events, COLOR_PAIR(COL_DIM));
		mvwprintw(win_events, 2, 1, "no events");
		wattroff(win_events, COLOR_PAIR(COL_DIM));
		wrefresh(win_events);
		return;
	}

	/* compute scroll offset to keep selected event visible */
	{
		int visible = max_h - 3; /* rows available for events (after header + gap) */
		int scroll = 0;
		if (visible > 0 && st->selected_event >= visible)
			scroll = st->selected_event - visible + 1;

	for (i = scroll; i < n; i++) {
		int y = (i - scroll) + 2;
		const struct event *ev = day_events[i];
		int selected = (i == st->selected_event);
		if (y >= max_h - 1)
			break;
		int cpair = 32 + ev->cal_idx;
		int ps = user_partstat(st, ev);
		int summary_end;

		if (selected) {
			wattron(win_events, COLOR_PAIR(COL_SELECTED) | A_BOLD);
			mvwhline(win_events, y, 0, ' ', max_w);
		}

		/* calendar color indicator */
		if (!selected)
			wattron(win_events, COLOR_PAIR(cpair));
		mvwprintw(win_events, y, 1, "●");
		if (!selected)
			wattroff(win_events, COLOR_PAIR(cpair));

		/* time */
		if (ev->all_day) {
			mvwprintw(win_events, y, 3, "all day");
		} else {
			localtime_r(&ev->start, &t);
			strftime(tbuf, sizeof(tbuf), time_fmt, &t);
			mvwprintw(win_events, y, 3, "%s", tbuf);
		}

		/* recurrence indicator */
		if (ev->recur_freq != RECUR_NONE || ev->is_recurrence) {
			if (!selected) wattron(win_events, COLOR_PAIR(COL_DIM));
			mvwprintw(win_events, y, ev->all_day ? 11 : 9, "↻");
			if (!selected) wattroff(win_events, COLOR_PAIR(COL_DIM));
		}

		/* summary (leave room for right-side indicators) */
		summary_end = max_w - 16;
		if (summary_end < 20) summary_end = max_w - 8;
		{
			int sum_x = ev->all_day ? 11 : 9;
			if (ev->recur_freq != RECUR_NONE || ev->is_recurrence)
				sum_x += 2;
			/* dim cancelled events */
			if (ev->status == STATUS_CANCELLED && !selected)
				wattron(win_events, COLOR_PAIR(COL_DIM));
			mvwprintw(win_events, y, sum_x, " %.*s",
			          summary_end - sum_x, ev->summary);
			if (ev->status == STATUS_CANCELLED && !selected)
				wattroff(win_events, COLOR_PAIR(COL_DIM));
		}

		/* right side: RSVP status + attendee count */
		if (ev->n_attendees > 0) {
			int rx = max_w - 14;
			if (rx < summary_end) rx = summary_end + 1;

			if (ps >= 0) {
				/* user's own RSVP status */
				const char *sym;
				int scol;
				switch (ps) {
				case PARTSTAT_ACCEPTED:    scol = COL_ACCEPTED;    sym = "✓"; break;
				case PARTSTAT_DECLINED:    scol = COL_DECLINED;    sym = "✗"; break;
				case PARTSTAT_TENTATIVE:   scol = COL_TENTATIVE;   sym = "?"; break;
				default:                   scol = COL_NEEDSACTION; sym = "!"; break;
				}
				if (!selected) wattron(win_events, COLOR_PAIR(scol));
				mvwprintw(win_events, y, rx, "%s", sym);
				if (!selected) wattroff(win_events, COLOR_PAIR(scol));
				rx += 2;
			}

			/* attendee count with accept ratio */
			{
				int ai, acc = 0;
				for (ai = 0; ai < ev->n_attendees; ai++) {
					if (ev->attendees[ai].status == PARTSTAT_ACCEPTED)
						acc++;
				}
				if (!selected) wattron(win_events, COLOR_PAIR(COL_DIM));
				mvwprintw(win_events, y, rx, "%d/%d",
				          acc, ev->n_attendees);
				if (!selected) wattroff(win_events, COLOR_PAIR(COL_DIM));
			}
		}

		if (selected)
			wattroff(win_events, COLOR_PAIR(COL_SELECTED) | A_BOLD);
	}
	} /* scroll block */

	wrefresh(win_events);
}

static void
draw_status(const struct state *st)
{
	int w = COLS;
	int readonly = 0;   /* bit 1: subscription, bit 2: not owner */
	int has_event = 0;

	/* items in priority order (highest first) */
	/* each item has a short and long form */
	struct hint {
		const char *text;    /* displayed text */
		int need_event;      /* 1 = only show when event selected */
		int hide_flags;      /* bitmask: 1=hide if readonly, 2=hide if not owner */
	};

	struct hint hints[] = {
		{ "q:quit",           0, 0 },
		{ "a:add",            0, 0 },
		{ "e:edit",           1, 3 },  /* hide if readonly OR not owner */
		{ "x:del",            1, 3 },
		{ "Ctrl-R:sync",      0, 0 },
		{ "r:rsvp",           1, 1 },  /* hide only if readonly (subscription) */
		{ "c:cal",            0, 0 },
		{ "hjkl:nav",         0, 0 },
		{ "H/L:month",        0, 0 },
		{ "Up/Dn:events",     0, 0 },
		{ "u/i:detail",       0, 0 },
		{ "o:today",          0, 0 },
		{ "F5/F6:upcoming",   0, 0 },
	};
	int nhints = (int)(sizeof(hints) / sizeof(hints[0]));

	char bar[512] = {0};
	int bar_len = 0;
	int i, x;
	const char *sep = "  ";
	int sep_len = 2;

	/* check if selected event is from a subscription (read-only) */
	{
		const struct event *day_events[256];
		int n = get_day_events(st, day_events, 256);
		if (n > 0 && st->selected_event >= 0 && st->selected_event < n) {
			has_event = 1;
			const struct event *ev = day_events[st->selected_event];
			if (ev->cal_idx >= 0 && ev->cal_idx < st->n_calendars &&
			    st->calendars[ev->cal_idx].subscription)
				readonly |= 1;
			/* hide edit/del if user is a non-organizer attendee */
			if (ev->organizer_email[0] && ev->n_attendees > 0) {
				int is_att = 0, ai;
				const char *me = cal_email(st, ev->cal_idx);
				for (ai = 0; ai < ev->n_attendees; ai++) {
					if (me[0] && strcasecmp(ev->attendees[ai].email, me) == 0) {
						is_att = 1;
						break;
					}
				}
				if (is_att && strcasecmp(me, ev->organizer_email) != 0)
					readonly |= 2;
			}
		}
	}

	/* build bar by appending items that fit */
	for (i = 0; i < nhints; i++) {
		int tlen = (int)strlen(hints[i].text);

		if (hints[i].need_event && !has_event)
			continue;
		if (hints[i].hide_flags & readonly)
			continue;

		/* check if it fits (with separator if not first) */
		int needed = tlen + (bar_len > 0 ? sep_len : 0);
		if (bar_len + needed > w - 2)
			continue;

		if (bar_len > 0) {
			strcat(bar, sep);
			bar_len += sep_len;
		}
		strcat(bar, hints[i].text);
		bar_len += tlen;
	}

	werase(win_status);
	wattron(win_status, COLOR_PAIR(COL_DIM));

	x = (w - bar_len) / 2;
	if (x < 1) x = 1;
	mvwprintw(win_status, 0, x, "%s", bar);

	wattroff(win_status, COLOR_PAIR(COL_DIM));
	wrefresh(win_status);
}

/* draw wrapped text, returns number of rows used */
static int
draw_wrapped(WINDOW *win, int row, int col, int max_w, int max_h,
             const char *text, int color_pair)
{
	int x = col;
	int rows = 0;
	const char *p = text;

	if (color_pair >= 0)
		wattron(win, COLOR_PAIR(color_pair));

	while (*p && row + rows < max_h) {
		if (*p == '\n') {
			rows++;
			x = col;
			p++;
			continue;
		}
		if (x >= max_w - 1) {
			rows++;
			x = col;
			if (row + rows >= max_h)
				break;
		}

		/* handle UTF-8 multi-byte sequences */
		unsigned char c = (unsigned char)*p;
		int seqlen = 1;
		if ((c & 0xe0) == 0xc0)      seqlen = 2;
		else if ((c & 0xf0) == 0xe0)  seqlen = 3;
		else if ((c & 0xf8) == 0xf0)  seqlen = 4;

		if (seqlen > 1) {
			char mb[5];
			int i;
			for (i = 0; i < seqlen && p[i]; i++)
				mb[i] = p[i];
			mb[i] = '\0';
			mvwaddstr(win, row + rows, x, mb);
			/* wide chars may take 2 columns; narrow ones take 1 */
			x++;
			p += i;
		} else {
			mvwaddch(win, row + rows, x, *p);
			x++;
			p++;
		}
	}
	if (x > col)
		rows++;

	if (color_pair >= 0)
		wattroff(win, COLOR_PAIR(color_pair));

	return rows;
}

static void
draw_detail(struct state *st)
{
	const struct event *day_events[256];
	int n, i;
	int max_h = getmaxy(win_detail);
	int max_w = getmaxx(win_detail);
	int ds = st->detail_scroll;
	struct tm t;
	char tbuf[64];
	const struct event *ev;

	werase(win_detail);

	/* horizontal separator at top */
	wattron(win_detail, COLOR_PAIR(COL_BORDER));
	mvwhline(win_detail, 0, 0, ACS_HLINE, max_w);
	wattroff(win_detail, COLOR_PAIR(COL_BORDER));

	n = get_day_events(st, day_events, 256);
	if (n == 0 || st->selected_event < 0 || st->selected_event >= n) {
		st->detail_scroll = 0;
		wrefresh(win_detail);
		return;
	}

	ev = day_events[st->selected_event];

	/*
	 * Render detail content into a large pad, then copy
	 * the visible portion (based on detail_scroll) into win_detail.
	 * This gives us smooth scrolling without per-line bounds checks.
	 */
	{
		int has_att = ev->n_attendees > 0;
		int right_w = has_att ? max_w / 3 : 0;
		if (right_w > 0 && right_w < 20) right_w = 20;
		if (right_w > max_w / 2) right_w = max_w / 2;
		int left_w = has_att ? max_w - right_w - 1 : max_w;

		int pad_h = max_h + 128; /* generous virtual height */
		WINDOW *pad = newpad(pad_h, left_w > 0 ? left_w : max_w);
		int prow = 0;
		int content_h;

		/* title */
		wattron(pad, A_BOLD | COLOR_PAIR(COL_ACCENT));
		mvwprintw(pad, prow, 1, "%.*s", left_w - 2, ev->summary);
		wattroff(pad, A_BOLD | COLOR_PAIR(COL_ACCENT));
		prow++;

		/* time */
		if (ev->all_day) {
			wattron(pad, COLOR_PAIR(COL_DIM));
			mvwprintw(pad, prow, 1, "all day");
			wattroff(pad, COLOR_PAIR(COL_DIM));
		} else {
			wattron(pad, COLOR_PAIR(COL_DIM));
			localtime_r(&ev->start, &t);
			strftime(tbuf, sizeof(tbuf), "%H:%M", &t);
			mvwprintw(pad, prow, 1, "%s", tbuf);
			localtime_r(&ev->end, &t);
			strftime(tbuf, sizeof(tbuf), "-%H:%M", &t);
			wprintw(pad, "%s", tbuf);
			wattroff(pad, COLOR_PAIR(COL_DIM));
		}

		if (ev->status != STATUS_NONE) {
			int scol;
			switch (ev->status) {
			case STATUS_CONFIRMED: scol = COL_ACCEPTED;  break;
			case STATUS_TENTATIVE: scol = COL_TENTATIVE;  break;
			case STATUS_CANCELLED: scol = COL_DECLINED;   break;
			default:               scol = COL_DIM;        break;
			}
			wprintw(pad, "  ");
			wattron(pad, COLOR_PAIR(scol));
			wprintw(pad, "%s", event_status_str(ev->status));
			wattroff(pad, COLOR_PAIR(scol));
		}
		prow++;

		/* location (own line) */
		if (ev->location[0]) {
			wattron(pad, COLOR_PAIR(COL_DIM));
			mvwprintw(pad, prow, 1, "@ ");
			wattroff(pad, COLOR_PAIR(COL_DIM));
			prow += draw_wrapped(pad, prow, 3, left_w - 1, pad_h,
			                     ev->location, -1);
		}

		/* recurrence */
		if (ev->recur_freq != RECUR_NONE) {
			wattron(pad, COLOR_PAIR(COL_DIM));
			mvwprintw(pad, prow, 1, "repeat: ");
			if (ev->recur_interval > 1)
				wprintw(pad, "every %d %s",
				        ev->recur_interval, recur_freq_str(ev->recur_freq));
			else
				wprintw(pad, "%s", recur_freq_str(ev->recur_freq));
			if (ev->recur_count > 0)
				wprintw(pad, " (%dx)", ev->recur_count);
			else if (ev->recur_until > 0) {
				struct tm ru;
				char rubuf[16];
				localtime_r(&ev->recur_until, &ru);
				strftime(rubuf, sizeof(rubuf), "%Y-%m-%d", &ru);
				wprintw(pad, " (until %s)", rubuf);
			}
			wattroff(pad, COLOR_PAIR(COL_DIM));
			prow++;
		}

		/* organizer */
		if (ev->organizer_email[0]) {
			wattron(pad, COLOR_PAIR(COL_DIM));
			mvwprintw(pad, prow, 1, "org ");
			wattroff(pad, COLOR_PAIR(COL_DIM));
			wprintw(pad, "%s",
			        ev->organizer_name[0] ? ev->organizer_name : ev->organizer_email);
			prow++;
		}

		/* notes (wrapped, with separator) */
		if (ev->description[0]) {
			prow++; /* blank line before notes */
			prow += draw_wrapped(pad, prow, 1, left_w - 1, pad_h,
			                     ev->description, COL_DIM);
		}

		/* clamp scroll to content */
		content_h = prow;
		if (ds > content_h - (max_h - 1))
			ds = content_h - (max_h - 1);
		if (ds < 0)
			ds = 0;
		st->detail_scroll = ds;

		/* copy visible portion from pad into win_detail */
		copywin(pad, win_detail,
		        ds, 0,              /* pad source (top-left) */
		        1, 0,               /* win dest (row 1, after separator) */
		        max_h - 1, left_w - 1,  /* win dest (bottom-right) */
		        0);                 /* non-destructive */

		delwin(pad);

		/* right panel: attendees */
		if (has_att) {
			int rcol = left_w;  /* column where right panel starts */
			int arow = 1;       /* row 0 is separator */

			/* vertical separator */
			wattron(win_detail, COLOR_PAIR(COL_BORDER));
			{
				int sr;
				for (sr = 1; sr < max_h; sr++)
					mvwaddch(win_detail, sr, rcol, ACS_VLINE);
			}
			wattroff(win_detail, COLOR_PAIR(COL_BORDER));

			rcol++; /* content starts after separator */

			/* RSVP status */
			{
				int ps = user_partstat(st, ev);
				if (ps >= 0) {
					const char *label;
					int scol;
					switch (ps) {
					case PARTSTAT_ACCEPTED:    scol = COL_ACCEPTED;    label = "Accepted"; break;
					case PARTSTAT_DECLINED:    scol = COL_DECLINED;    label = "Declined"; break;
					case PARTSTAT_TENTATIVE:   scol = COL_TENTATIVE;   label = "Tentative"; break;
					default:                   scol = COL_NEEDSACTION; label = "Needs action"; break;
					}
					wattron(win_detail, COLOR_PAIR(COL_DIM));
					mvwprintw(win_detail, arow, rcol + 1, "rsvp ");
					wattroff(win_detail, COLOR_PAIR(COL_DIM));
					wattron(win_detail, COLOR_PAIR(scol) | A_BOLD);
					wprintw(win_detail, "%s", label);
					wattroff(win_detail, COLOR_PAIR(scol) | A_BOLD);
					arow++;
				}
			}

			/* attendee list */
			{
				const char *user_em = cal_email(st, ev->cal_idx);
				int att_w = right_w - 2; /* usable width */

				for (i = 0; i < ev->n_attendees && arow < max_h; i++) {
					const struct attendee *att = &ev->attendees[i];
					const char *sym;
					int scol;

					switch (att->status) {
					case PARTSTAT_ACCEPTED:    scol = COL_ACCEPTED;    sym = "+"; break;
					case PARTSTAT_DECLINED:    scol = COL_DECLINED;    sym = "-"; break;
					case PARTSTAT_TENTATIVE:   scol = COL_TENTATIVE;   sym = "?"; break;
					case PARTSTAT_NEEDSACTION: scol = COL_NEEDSACTION; sym = "*"; break;
					default:                   scol = COL_DIM;         sym = " "; break;
					}

					wattron(win_detail, COLOR_PAIR(scol));
					mvwprintw(win_detail, arow, rcol + 1, "%s", sym);
					wattroff(win_detail, COLOR_PAIR(scol));

					wprintw(win_detail, " %.*s", att_w - 3,
					        att->name[0] ? att->name : att->email);

					if (user_em[0] && strcasecmp(att->email, user_em) == 0) {
						wattron(win_detail, COLOR_PAIR(COL_ACCENT));
						wprintw(win_detail, " (you)");
						wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
					}
					arow++;
				}
			}
		}
	}

	wrefresh(win_detail);
}

void
ui_draw(struct state *st)
{
	/* resize windows on terminal resize */
	{
		int mid_w = COLS - SIDEBAR_W - CAL_PANEL_W;
		int body_h = LINES - HEADER_H - STATUS_H;
		int ev_h = body_h / 2;
		int det_h = body_h - ev_h;

		wresize(win_header, HEADER_H, COLS);
		wresize(win_sidebar, body_h, SIDEBAR_W);
		mvwin(win_sidebar, HEADER_H, 0);
		wresize(win_events, ev_h, mid_w);
		mvwin(win_events, HEADER_H, SIDEBAR_W);
		wresize(win_detail, det_h, mid_w);
		mvwin(win_detail, HEADER_H + ev_h, SIDEBAR_W);
		wresize(win_cal, body_h, CAL_PANEL_W);
		mvwin(win_cal, HEADER_H, COLS - CAL_PANEL_W);
		wresize(win_status, STATUS_H, COLS);
		mvwin(win_status, LINES - STATUS_H, 0);
	}

	draw_header(st);
	draw_sidebar(st);
	draw_month_cal(st);
	draw_events(st);
	draw_detail(st);
	draw_status(st);
}

int
ui_rsvp_prompt(void)
{
	int ch;

	/* inline prompt in status bar (centered) */
	{
		/* "RSVP: accept  decline  tentative  Esc cancel" = 46 chars */
		int sx = (COLS - 46) / 2;
		if (sx < 1) sx = 1;
		werase(win_status);
		mvwprintw(win_status, 0, sx, "RSVP: ");
		wattron(win_status, COLOR_PAIR(COL_ACCEPTED));
		wprintw(win_status, "a");
		wattroff(win_status, COLOR_PAIR(COL_ACCEPTED));
		wprintw(win_status, "ccept  ");
		wattron(win_status, COLOR_PAIR(COL_DECLINED));
		wprintw(win_status, "d");
		wattroff(win_status, COLOR_PAIR(COL_DECLINED));
		wprintw(win_status, "ecline  ");
		wattron(win_status, COLOR_PAIR(COL_TENTATIVE));
		wprintw(win_status, "t");
		wattroff(win_status, COLOR_PAIR(COL_TENTATIVE));
		wprintw(win_status, "entative  ");
		wattron(win_status, COLOR_PAIR(COL_DIM));
		wprintw(win_status, "Esc cancel");
		wattroff(win_status, COLOR_PAIR(COL_DIM));
		wrefresh(win_status);
	}

	ch = wgetch(win_status);

	switch (ch) {
	case 'a': return PARTSTAT_ACCEPTED;
	case 'd': return PARTSTAT_DECLINED;
	case 't': return PARTSTAT_TENTATIVE;
	default:  return -1;
	}
}

int
ui_confirm(const char *msg)
{
	int ch;

	/* inline prompt in status bar (centered) */
	{
		int total = (int)strlen(msg) + 1 + 3; /* "msg y/n" */
		int sx = (COLS - total) / 2;
		if (sx < 1) sx = 1;
		werase(win_status);
		mvwprintw(win_status, 0, sx, "%s ", msg);
		wattron(win_status, COLOR_PAIR(COL_ACCENT));
		wprintw(win_status, "y");
		wattroff(win_status, COLOR_PAIR(COL_ACCENT));
		wattron(win_status, COLOR_PAIR(COL_DIM));
		wprintw(win_status, "/n");
		wattroff(win_status, COLOR_PAIR(COL_DIM));
		wrefresh(win_status);
	}

	ch = wgetch(win_status);

	return (ch == 'y' || ch == 'Y');
}

/*
 * Form system — inline editing, arrow-navigable fields.
 * Typing goes directly into the selected field (no Enter-to-edit).
 * Up/Down/Tab navigate between fields. Enter saves. Esc cancels.
 * Each field can have a validator that filters keypresses.
 */
#define FORM_MAX_FIELDS 12

/* validator: return 1 to accept the character, 0 to reject */
typedef int (*field_validator)(int ch, const char *buf, int len);

struct form_field {
	const char *label;
	char *buf;
	int maxlen;
	int readonly;         /* display-only, skip during navigation */
	int password;         /* mask input with * */
	int required;         /* must be non-empty to submit */
	int multiline;        /* Enter inserts newline, Ctrl-D saves */
	field_validator validate; /* NULL = accept anything printable */
};

/* built-in validators */
static int
validate_time(int ch, const char *buf, int len)
{
	/* accept digits and colon, enforce HH:MM pattern */
	if (ch == ':') return len > 0 && len < 3;
	if (ch >= '0' && ch <= '9') {
		if (len == 0) return ch <= '2';           /* H: 0-2 */
		if (len == 1) return buf[0] < '2' || ch <= '3'; /* H: x0-x3 if 2x */
		if (len == 2) return 0;                   /* must be : */
		if (len == 3) return ch <= '5';           /* M: 0-5 */
		if (len == 4) return 1;                   /* M: x0-x9 */
		return 0;
	}
	return 0;
}

static int
validate_number(int ch, const char *buf, int len)
{
	(void)buf;
	(void)len;
	return (ch >= '0' && ch <= '9');
}

/*
 * form_run — draw and run an interactive form in the detail panel.
 * Returns 0 on submit (Ctrl-D/Enter on last field), -1 on cancel (Esc).
 *
 * Supports free cursor navigation: LEFT/RIGHT move within text,
 * UP/DOWN navigate between visual lines in multiline fields
 * (or between fields for single-line fields). Characters are
 * inserted/deleted at the cursor position.
 */
static int
form_run(WINDOW *win, struct form_field *fields, int nfields,
         int start_row, int col_hint, const char *title)
{
	int cur = 0;  /* index into editable[] */
	int editable[FORM_MAX_FIELDS];
	int n_editable = 0;
	int i, ch;
	char errmsg[64] = {0};
	int cpos = 0;       /* cursor position within current field buffer */
	int prev_cur = -1;  /* detect field changes to reset cpos */

	int is_inline = (win == win_detail);
	(void)col_hint;

	for (i = 0; i < nfields; i++) {
		if (!fields[i].readonly)
			editable[n_editable++] = i;
	}

	if (n_editable == 0)
		return -1;

	curs_set(1);
	keypad(win, TRUE);
	wtimeout(win, -1);

	for (;;) {
		int fi = editable[cur];
		int cursor_row = 0, cursor_col = 0;
		int win_h = getmaxy(win);
		int win_w = getmaxx(win);
		int col, row;
		int pad_h = win_h + 256; /* virtual height for scrolling */
		WINDOW *rw;              /* render window (pad or win) */
		int render_h;

		/* multiline line-start map (for UP/DOWN navigation) */
		int ml_lstarts[512];
		int ml_nlines = 0;
		(void)0; /* ml navigation data populated during render */

		/* reset cpos when switching fields */
		if (cur != prev_cur) {
			cpos = (int)strlen(fields[fi].buf);
			prev_cur = cur;
		}

		/* clamp cpos */
		{
			int flen = (int)strlen(fields[fi].buf);
			if (cpos > flen) cpos = flen;
			if (cpos < 0) cpos = 0;
		}

		if (is_inline) {
			rw = newpad(pad_h, win_w);
			render_h = pad_h;
			col = 1;
			row = 1; /* row 0 reserved for separator */
			if (title) {
				wattron(rw, A_BOLD | COLOR_PAIR(COL_ACCENT));
				mvwprintw(rw, row, 1, "%s", title);
				wattroff(rw, A_BOLD | COLOR_PAIR(COL_ACCENT));
				row++;
			}
		} else {
			rw = win;
			render_h = win_h;
			werase(win);
			draw_header(NULL);
			row = start_row > 0 ? start_row : HEADER_H + 1;

			int max_label = 0;
			for (i = 0; i < nfields; i++) {
				int ll = (int)strlen(fields[i].label);
				if (ll > max_label) max_label = ll;
			}
			col = (win_w - max_label - 30) / 2;
			if (col < 4) col = 4;

			if (title) {
				int title_len = (int)strlen(title) + 2;
				int tx = (win_w - title_len) / 2;
				if (tx < 0) tx = 0;
				wattron(rw, A_BOLD | COLOR_PAIR(COL_ACCENT));
				mvwprintw(rw, row, tx, " %s ", title);
				wattroff(rw, A_BOLD | COLOR_PAIR(COL_ACCENT));
				wattron(rw, COLOR_PAIR(COL_BORDER));
				mvwhline(rw, row + 1, tx, ACS_HLINE, title_len);
				wattroff(rw, COLOR_PAIR(COL_BORDER));
				row += 3;
			}
		}

		/* draw fields */
		for (i = 0; i < nfields && row < render_h; i++) {
			int is_cur = (i == fi);
			int label_len = (int)strlen(fields[i].label);

			if (fields[i].readonly) {
				wattron(rw, COLOR_PAIR(COL_DIM));
				mvwprintw(rw, row, col, "%s", fields[i].label);
				wattroff(rw, COLOR_PAIR(COL_DIM));
				wprintw(rw, "%s", fields[i].buf);
				row++;
				continue;
			}

			/* label */
			if (is_cur)
				wattron(rw, COLOR_PAIR(COL_ACCENT));
			else
				wattron(rw, COLOR_PAIR(COL_DIM));
			mvwprintw(rw, row, col, "%s", fields[i].label);
			if (is_cur)
				wattroff(rw, COLOR_PAIR(COL_ACCENT));
			else
				wattroff(rw, COLOR_PAIR(COL_DIM));

			/* value */
			if (fields[i].multiline && fields[i].buf[0]) {
				row++;
				if (is_cur)
					wattron(rw, A_BOLD);
				{
					int ml_ind = col + 2;
					int ml_x = ml_ind;
					int ml_row = row;
					const char *base = fields[i].buf;
					const char *p = base;
					int pos;

					/* build line-start map for navigation */
					if (is_cur) {
						ml_nlines = 0;
						ml_lstarts[ml_nlines++] = 0;
					}

					wmove(rw, ml_row, ml_x);
					while (*p && ml_row < render_h - 1) {
						pos = (int)(p - base);
						if (is_cur && pos == cpos) {
							cursor_row = ml_row;
							cursor_col = ml_x;
						}
						/* UTF-8 sequence length */
						unsigned char uc = (unsigned char)*p;
						int sq = 1;
						if ((uc & 0xE0) == 0xC0)      sq = 2;
						else if ((uc & 0xF0) == 0xE0)  sq = 3;
						else if ((uc & 0xF8) == 0xF0)  sq = 4;

						if (*p == '\n') {
							ml_row++;
							ml_x = ml_ind;
							if (is_cur && ml_nlines < 512)
								ml_lstarts[ml_nlines++] = pos + 1;
							wmove(rw, ml_row, ml_x);
							p++;
						} else if (ml_x >= win_w - 2) {
							ml_row++;
							ml_x = ml_ind;
							if (is_cur && ml_nlines < 512)
								ml_lstarts[ml_nlines++] = pos;
							wmove(rw, ml_row, ml_x);
							waddnstr(rw, p, sq);
							ml_x++;
							p += sq;
						} else {
							waddnstr(rw, p, sq);
							ml_x++;
							p += sq;
						}
					}
					/* cursor at end of text */
					if (is_cur && (int)(p - base) == cpos) {
						cursor_row = ml_row;
						cursor_col = ml_x;
					}
					row = ml_row + 1;
				}
				if (is_cur)
					wattroff(rw, A_BOLD);
			} else if (fields[i].buf[0]) {
				if (is_cur)
					wattron(rw, A_BOLD);
				if (fields[i].password) {
					int pi;
					for (pi = 0; fields[i].buf[pi]; pi++)
						wprintw(rw, "*");
				} else {
					wprintw(rw, "%s", fields[i].buf);
				}
				if (is_cur)
					wattroff(rw, A_BOLD);

				if (is_cur) {
					/* count display columns (not bytes) up to cpos */
					int dc = 0, bi;
					for (bi = 0; bi < cpos && fields[fi].buf[bi]; ) {
						unsigned char bc = (unsigned char)fields[fi].buf[bi];
						int bsq = 1;
						if ((bc & 0xE0) == 0xC0)      bsq = 2;
						else if ((bc & 0xF0) == 0xE0)  bsq = 3;
						else if ((bc & 0xF8) == 0xF0)  bsq = 4;
						dc++;
						bi += bsq;
					}
					cursor_row = row;
					cursor_col = col + label_len + dc;
				}
				row++;
			} else {
				if (!is_cur) {
					wattron(rw, COLOR_PAIR(COL_DIM));
					wprintw(rw, "-");
					wattroff(rw, COLOR_PAIR(COL_DIM));
				}
				if (is_cur) {
					cursor_row = row;
					cursor_col = col + label_len;
				}
				row++;
			}
		}

		/* error message */
		if (errmsg[0] && row < render_h) {
			wattron(rw, COLOR_PAIR(COL_DECLINED));
			mvwprintw(rw, row, col, "%s", errmsg);
			wattroff(rw, COLOR_PAIR(COL_DECLINED));
		}

		/* hint + display */
		{
			const char *hint = "Tab/Enter:next  Ctrl-D:save  Esc:cancel";
			int hint_len = (int)strlen(hint);
			if (is_inline) {
				/* compute scroll so cursor is visible */
				int scroll = 0;
				if (cursor_row >= win_h)
					scroll = cursor_row - win_h + 1;

				werase(win);
				copywin(rw, win,
				        scroll, 0,
				        0, 0,
				        win_h - 1, win_w - 1,
				        0);
				delwin(rw);

				/* draw separator at top (always visible) */
				wattron(win, COLOR_PAIR(COL_BORDER));
				mvwhline(win, 0, 0, ACS_HLINE, win_w);
				wattroff(win, COLOR_PAIR(COL_BORDER));

				/* place cursor in the window */
				cursor_row -= scroll;
				if (cursor_row < 0) cursor_row = 0;
				if (cursor_row >= win_h) cursor_row = win_h - 1;

				/* refresh status bar FIRST so cursor ends up in detail */
				int hx = (COLS - hint_len) / 2;
				if (hx < 1) hx = 1;
				werase(win_status);
				wattron(win_status, COLOR_PAIR(COL_DIM));
				mvwprintw(win_status, 0, hx, "%s", hint);
				wattroff(win_status, COLOR_PAIR(COL_DIM));
				wrefresh(win_status);

				wmove(win, cursor_row, cursor_col);
				wrefresh(win);
			} else {
				int hx = (win_w - hint_len) / 2;
				if (hx < 1) hx = 1;
				wattron(win, COLOR_PAIR(COL_DIM));
				mvwprintw(win, win_h - 1, hx, "%s", hint);
				wattroff(win, COLOR_PAIR(COL_DIM));
				wmove(win, cursor_row, cursor_col);
				wrefresh(win);
			}
		}

		ch = wgetch(win);
		errmsg[0] = '\0';

		if (ch == 27) { /* Esc */
			curs_set(0);
			return -1;
		}

		if (ch == '\n') {
			/* in multiline fields, Enter inserts newline at cpos */
			if (fields[fi].multiline) {
				int len = (int)strlen(fields[fi].buf);
				if (len < fields[fi].maxlen - 1) {
					memmove(&fields[fi].buf[cpos + 1],
					        &fields[fi].buf[cpos],
					        len - cpos + 1);
					fields[fi].buf[cpos] = '\n';
					cpos++;
				}
				continue;
			}
			/* on last editable field, Enter saves */
			if (cur == n_editable - 1) {
				int valid = 1;
				for (i = 0; i < nfields; i++) {
					if (fields[i].required && !fields[i].readonly
					    && fields[i].buf[0] == '\0') {
						snprintf(errmsg, sizeof(errmsg),
						         "%s is required",
						         fields[i].label);
						{
							int ei;
							for (ei = 0; ei < n_editable; ei++) {
								if (editable[ei] == i) {
									cur = ei;
									break;
								}
							}
						}
						valid = 0;
						break;
					}
				}
				if (valid) {
					curs_set(0);
					return 0;
				}
				continue;
			}
			/* move to next field */
			if (cur < n_editable - 1)
				cur++;
			continue;
		}

		/* Ctrl-D: save (works from any field, needed for multiline) */
		if (ch == 4) {
			int valid = 1;
			for (i = 0; i < nfields; i++) {
				if (fields[i].required && !fields[i].readonly
				    && fields[i].buf[0] == '\0') {
					snprintf(errmsg, sizeof(errmsg),
					         "%s is required",
					         fields[i].label);
					{
						int ei;
						for (ei = 0; ei < n_editable; ei++) {
							if (editable[ei] == i) {
								cur = ei;
								break;
							}
						}
					}
					valid = 0;
					break;
				}
			}
			if (valid) {
				curs_set(0);
				return 0;
			}
			continue;
		}

		if (ch == KEY_LEFT) {
			if (cpos > 0) {
				cpos--;
				/* skip over UTF-8 continuation bytes */
				while (cpos > 0 &&
				       ((unsigned char)fields[fi].buf[cpos] & 0xC0) == 0x80)
					cpos--;
			}
			continue;
		}

		if (ch == KEY_RIGHT) {
			int len = (int)strlen(fields[fi].buf);
			if (cpos < len) {
				/* skip full UTF-8 sequence */
				unsigned char c = (unsigned char)fields[fi].buf[cpos];
				int skip = 1;
				if ((c & 0xE0) == 0xC0)      skip = 2;
				else if ((c & 0xF0) == 0xE0)  skip = 3;
				else if ((c & 0xF8) == 0xF0)  skip = 4;
				cpos += skip;
				if (cpos > len) cpos = len;
			}
			continue;
		}

		if (ch == KEY_UP) {
			if (fields[fi].multiline && ml_nlines > 0) {
				/* find which visual line cursor is on */
				int cl = 0, li;
				for (li = 1; li < ml_nlines; li++) {
					if (cpos >= ml_lstarts[li])
						cl = li;
					else
						break;
				}
				if (cl > 0) {
					/* move to same column on previous line */
					int col_off = cpos - ml_lstarts[cl];
					int prev_end = ml_lstarts[cl];
					/* exclude newline at end of previous line */
					if (prev_end > ml_lstarts[cl - 1] &&
					    fields[fi].buf[prev_end - 1] == '\n')
						prev_end--;
					int prev_len = prev_end - ml_lstarts[cl - 1];
					if (col_off > prev_len)
						col_off = prev_len;
					cpos = ml_lstarts[cl - 1] + col_off;
					continue;
				}
			}
			/* first line or non-multiline: previous field */
			if (cur > 0) cur--;
			continue;
		}

		if (ch == KEY_DOWN) {
			if (fields[fi].multiline && ml_nlines > 0) {
				/* find which visual line cursor is on */
				int cl = 0, li;
				for (li = 1; li < ml_nlines; li++) {
					if (cpos >= ml_lstarts[li])
						cl = li;
					else
						break;
				}
				if (cl + 1 < ml_nlines) {
					/* move to same column on next line */
					int col_off = cpos - ml_lstarts[cl];
					int next_end;
					if (cl + 2 < ml_nlines)
						next_end = ml_lstarts[cl + 2];
					else
						next_end = (int)strlen(fields[fi].buf);
					/* exclude newline at end of next line */
					if (next_end > ml_lstarts[cl + 1] &&
					    fields[fi].buf[next_end - 1] == '\n')
						next_end--;
					int next_len = next_end - ml_lstarts[cl + 1];
					if (col_off > next_len)
						col_off = next_len;
					cpos = ml_lstarts[cl + 1] + col_off;
					continue;
				}
			}
			/* last line or non-multiline: next field */
			if (cur < n_editable - 1) cur++;
			continue;
		}

		if (ch == '\t') {
			if (cur < n_editable - 1) cur++;
			continue;
		}

		if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (cpos > 0) {
				int len = (int)strlen(fields[fi].buf);
				/* find start of UTF-8 sequence before cursor */
				int del = 1;
				while (del < cpos &&
				       ((unsigned char)fields[fi].buf[cpos - del] & 0xC0) == 0x80)
					del++;
				memmove(&fields[fi].buf[cpos - del],
				        &fields[fi].buf[cpos],
				        len - cpos + 1);
				cpos -= del;
			}
			continue;
		}

		/* printable character — insert at cursor position */
		if (ch >= 32 && ch < 127) {
			int len = (int)strlen(fields[fi].buf);
			if (len >= fields[fi].maxlen - 1) {
				snprintf(errmsg, sizeof(errmsg),
				         "Field is full (%d chars max)",
				         fields[fi].maxlen - 1);
				continue;
			}

			/* validate if validator exists */
			if (fields[fi].validate) {
				if (!fields[fi].validate(ch, fields[fi].buf, len)) {
					snprintf(errmsg, sizeof(errmsg),
					         "Invalid input");
					continue;
				}
			}

			memmove(&fields[fi].buf[cpos + 1],
			        &fields[fi].buf[cpos],
			        len - cpos + 1);
			fields[fi].buf[cpos] = (char)ch;
			cpos++;
		}

		/* UTF-8 multi-byte input */
		if (ch >= 0xC0 && ch <= 0xF7) {
			unsigned char lead = (unsigned char)ch;
			int seqlen = 0;
			char mb[5];
			int bi;

			if ((lead & 0xE0) == 0xC0)      seqlen = 2;
			else if ((lead & 0xF0) == 0xE0)  seqlen = 3;
			else if ((lead & 0xF8) == 0xF0)  seqlen = 4;

			if (seqlen > 0) {
				int len = (int)strlen(fields[fi].buf);
				if (len + seqlen >= fields[fi].maxlen) {
					/* discard continuation bytes */
					for (bi = 1; bi < seqlen; bi++)
						wgetch(win);
					continue;
				}

				mb[0] = (char)lead;
				int valid = 1;
				for (bi = 1; bi < seqlen; bi++) {
					int cb = wgetch(win);
					if ((cb & 0xC0) != 0x80) {
						valid = 0;
						break;
					}
					mb[bi] = (char)cb;
				}
				if (!valid)
					continue;

				memmove(&fields[fi].buf[cpos + seqlen],
				        &fields[fi].buf[cpos],
				        len - cpos + 1);
				memcpy(&fields[fi].buf[cpos], mb, seqlen);
				cpos += seqlen;
			}
		}
	}
}

/* validate calendar name: alphanumeric, dash, underscore, dot */
static int
validate_cal_name(int ch, const char *buf, int len)
{
	(void)buf; (void)len;
	return ((ch >= 'a' && ch <= 'z') ||
	        (ch >= 'A' && ch <= 'Z') ||
	        (ch >= '0' && ch <= '9') ||
	        ch == '-' || ch == '_' || ch == '.');
}

/* validate recurrence input: n=none d=daily w=weekly m=monthly y=yearly */
static int
validate_recur(int ch, const char *buf, int len)
{
	(void)buf;
	if (len > 0) return 0; /* single char only */
	return (ch == 'n' || ch == 'd' || ch == 'w' || ch == 'm' || ch == 'y');
}

/* validate status input: c=confirmed t=tentative x=cancelled n=none */
static int
validate_status(int ch, const char *buf, int len)
{
	(void)buf;
	if (len > 0) return 0;
	return (ch == 'c' || ch == 't' || ch == 'x' || ch == 'n');
}

int
ui_event_editor(struct state *st, struct event *ev, int is_new)
{
	int nf = 0;
	struct form_field fields[FORM_MAX_FIELDS];
	char datebuf[32];
	char cal_label[MAX_NAME_LEN + 16];
	char title_buf[MAX_SUMMARY_LEN] = {0};
	char start_buf[16] = {0};
	char dur_buf[16] = {0};
	char loc_buf[MAX_LOCATION_LEN] = {0};
	char notes_buf[MAX_SUMMARY_LEN * 4] = {0};
	char status_buf[4] = {0};
	char recur_buf[4] = {0};
	char recur_count_buf[16] = {0};

	/* date (readonly) */
	strftime(datebuf, sizeof(datebuf), "%a %b %d, %Y", &st->cursor);
	fields[nf].label = "Date: ";
	fields[nf].buf = datebuf;
	fields[nf].maxlen = sizeof(datebuf);
	fields[nf].readonly = 1;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = NULL;
	nf++;

	/* calendar (readonly — already chosen via picker) */
	if (is_new) {
		snprintf(cal_label, sizeof(cal_label), "%s",
		         st->calendars[ev->cal_idx].name);
		fields[nf].label = "Calendar: ";
		fields[nf].buf = cal_label;
		fields[nf].maxlen = sizeof(cal_label);
		fields[nf].readonly = 1;
		fields[nf].password = 0;
		fields[nf].required = 0;
		fields[nf].multiline = 0;
		fields[nf].validate = NULL;
		nf++;
	}

	/* pre-fill buffers for edit mode */
	if (!is_new && ev->summary[0])
		strncpy(title_buf, ev->summary, MAX_SUMMARY_LEN - 1);
	if (!is_new) {
		struct tm t;
		localtime_r(&ev->start, &t);
		strftime(start_buf, sizeof(start_buf), "%H:%M", &t);
		int dur = (int)(ev->end - ev->start) / 60;
		if (dur > 0)
			snprintf(dur_buf, sizeof(dur_buf), "%d", dur);
	}
	if (!is_new && ev->location[0])
		strncpy(loc_buf, ev->location, MAX_LOCATION_LEN - 1);
	if (!is_new && ev->description[0])
		strncpy(notes_buf, ev->description, sizeof(notes_buf) - 1);

	/* pre-fill status (empty for new events) */
	switch (ev->status) {
	case STATUS_CONFIRMED: status_buf[0] = 'c'; break;
	case STATUS_TENTATIVE: status_buf[0] = 't'; break;
	case STATUS_CANCELLED: status_buf[0] = 'x'; break;
	default:               status_buf[0] = '\0'; break;
	}

	/* pre-fill recurrence (empty for new events) */
	switch (ev->recur_freq) {
	case RECUR_DAILY:   recur_buf[0] = 'd'; break;
	case RECUR_WEEKLY:  recur_buf[0] = 'w'; break;
	case RECUR_MONTHLY: recur_buf[0] = 'm'; break;
	case RECUR_YEARLY:  recur_buf[0] = 'y'; break;
	default:            recur_buf[0] = '\0'; break;
	}

	if (ev->recur_count > 0)
		snprintf(recur_count_buf, sizeof(recur_count_buf),
		         "%d", ev->recur_count);
	else if (ev->recur_until > 0) {
		struct tm ru;
		localtime_r(&ev->recur_until, &ru);
		strftime(recur_count_buf, sizeof(recur_count_buf),
		         "%Y-%m-%d", &ru);
	}

	/* title (required) */
	fields[nf].label = "Title: ";
	fields[nf].buf = title_buf;
	fields[nf].maxlen = MAX_SUMMARY_LEN;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 1;
	fields[nf].multiline = 0;
	fields[nf].validate = NULL;
	nf++;

	/* start time */
	fields[nf].label = "Start (HH:MM): ";
	fields[nf].buf = start_buf;
	fields[nf].maxlen = 6;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 1;
	fields[nf].multiline = 0;
	fields[nf].validate = validate_time;
	nf++;

	/* duration */
	fields[nf].label = "Duration (min): ";
	fields[nf].buf = dur_buf;
	fields[nf].maxlen = 6;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = validate_number;
	nf++;

	/* location */
	fields[nf].label = "Location: ";
	fields[nf].buf = loc_buf;
	fields[nf].maxlen = MAX_LOCATION_LEN;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = NULL;
	nf++;

	/* status (c/t/x/n) */
	fields[nf].label = "Status (c/t/x/n): ";
	fields[nf].buf = status_buf;
	fields[nf].maxlen = 2;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = validate_status;
	nf++;

	/* recurrence (n/d/w/m/y) */
	fields[nf].label = "Repeat (n/d/w/m/y): ";
	fields[nf].buf = recur_buf;
	fields[nf].maxlen = 2;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = validate_recur;
	nf++;

	/* recurrence end: number = count, YYYY-MM-DD = until date */
	fields[nf].label = "Repeat until (N or YYYY-MM-DD): ";
	fields[nf].buf = recur_count_buf;
	fields[nf].maxlen = 16;
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 0;
	fields[nf].validate = NULL;
	nf++;

	/* notes (multiline) */
	fields[nf].label = "Notes:";
	fields[nf].buf = notes_buf;
	fields[nf].maxlen = sizeof(notes_buf);
	fields[nf].readonly = 0;
	fields[nf].password = 0;
	fields[nf].required = 0;
	fields[nf].multiline = 1;
	fields[nf].validate = NULL;
	nf++;

	int ret = form_run(win_detail, fields, nf, 0, 1,
	                   is_new ? "New Event" : "Edit Event");

	if (ret != 0)
		return -1;

	/* apply values */
	strncpy(ev->summary, title_buf, MAX_SUMMARY_LEN - 1);
	ev->summary[MAX_SUMMARY_LEN - 1] = '\0';

	/* notes */
	strncpy(ev->description, notes_buf, sizeof(ev->description) - 1);
	ev->description[sizeof(ev->description) - 1] = '\0';

	/* status */
	switch (status_buf[0]) {
	case 'c': ev->status = STATUS_CONFIRMED; break;
	case 't': ev->status = STATUS_TENTATIVE; break;
	case 'x': ev->status = STATUS_CANCELLED; break;
	default:  ev->status = STATUS_NONE;      break;
	}

	/* recurrence */
	switch (recur_buf[0]) {
	case 'd': ev->recur_freq = RECUR_DAILY;   break;
	case 'w': ev->recur_freq = RECUR_WEEKLY;  break;
	case 'm': ev->recur_freq = RECUR_MONTHLY; break;
	case 'y': ev->recur_freq = RECUR_YEARLY;  break;
	default:  ev->recur_freq = RECUR_NONE;    break;
	}
	ev->recur_interval = 1;
	ev->recur_count = 0;
	ev->recur_until = 0;
	if (recur_count_buf[0]) {
		/* if it contains a dash, parse as YYYY-MM-DD date */
		if (strchr(recur_count_buf, '-')) {
			struct tm ru;
			memset(&ru, 0, sizeof(ru));
			if (strptime(recur_count_buf, "%Y-%m-%d", &ru)) {
				ru.tm_hour = 23;
				ru.tm_min = 59;
				ru.tm_sec = 59;
				ev->recur_until = mktime(&ru);
			}
		} else {
			ev->recur_count = atoi(recur_count_buf);
		}
	}

	/* location */
	strncpy(ev->location, loc_buf, MAX_LOCATION_LEN - 1);
	ev->location[MAX_LOCATION_LEN - 1] = '\0';

	/* set defaults for new events */
	if (is_new) {
		struct tm t;
		memset(&t, 0, sizeof(t));
		t.tm_year = st->cursor.tm_year;
		t.tm_mon = st->cursor.tm_mon;
		t.tm_mday = st->cursor.tm_mday;
		t.tm_hour = 9;
		t.tm_isdst = -1;
		ev->start = mktime(&t);
		t.tm_hour = 10;
		ev->end = mktime(&t);

		if (st->n_calendars > 0) {
			char *path = vdir_new_ics_path(st->calendars[ev->cal_idx].path);
			strncpy(ev->ics_path, path, MAX_PATH_LEN - 1);
		}

		snprintf(ev->uid, MAX_SUMMARY_LEN, "kc-%lx-%x",
		         (long)time(NULL), getpid());
	}

	/* parse start time */
	if (start_buf[0]) {
		int hh = 0, mm = 0;
		if (sscanf(start_buf, "%d:%d", &hh, &mm) >= 1) {
			struct tm t;
			localtime_r(&ev->start, &t);
			t.tm_hour = hh;
			t.tm_min = mm;
			t.tm_isdst = -1;
			ev->start = mktime(&t);
			t.tm_hour += 1;
			ev->end = mktime(&t);
		}
	}

	/* parse duration */
	if (dur_buf[0]) {
		int dur = atoi(dur_buf);
		if (dur > 0)
			ev->end = ev->start + dur * 60;
	}

	return 0;
}

int
ui_input(struct state *st)
{
	int ch = getch();
	const struct event *day_events[256];
	int n_day;

	if (ch == ERR)
		return 0; /* timeout, no input */

	switch (ch) {
	/* --- arrows: event list navigation --- */
	case KEY_UP:
		if (st->selected_event > 0) {
			st->selected_event--;
			st->detail_scroll = 0;
		}
		break;

	case KEY_DOWN:
		n_day = get_day_events(st, day_events, 256);
		if (st->selected_event < n_day - 1) {
			st->selected_event++;
			st->detail_scroll = 0;
		}
		break;

	/* --- detail panel scroll --- */
	case KEY_PPAGE:  /* Page Up */
	case KEY_DETAIL_UP:
		if (st->detail_scroll > 0) {
			st->detail_scroll -= 3;
			if (st->detail_scroll < 0)
				st->detail_scroll = 0;
		}
		break;

	case KEY_NPAGE:  /* Page Down */
	case KEY_DETAIL_DOWN:
		st->detail_scroll += 3;
		break;

	/* --- hjkl: calendar grid navigation (vim-style) --- */
	case KEY_CAL_UP:
		cal_prev_week(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_CAL_DOWN:
		cal_next_week(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_CAL_LEFT:
		cal_prev_day(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_CAL_RIGHT:
		cal_next_day(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	/* --- month navigation --- */
	case KEY_PREV_MONTH:
		cal_prev_month(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_NEXT_MONTH:
		cal_next_month(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_TODAY:
		cal_set_today(&st->cursor);
		st->selected_event = 0;
		st->detail_scroll = 0;
		break;

	case KEY_ADD_EVENT: {
		struct event new_ev;
		int cal_choice = -1;

		memset(&new_ev, 0, sizeof(new_ev));

		/* build list of writable (non-subscription) calendars */
		{
			int writable[MAX_CALENDARS];
			int n_writable = 0;
			int pi;

			for (pi = 0; pi < st->n_calendars; pi++) {
				if (!st->calendars[pi].subscription)
					writable[n_writable++] = pi;
			}

			if (n_writable == 0) {
				/* no writable calendars */
				break;
			} else if (n_writable == 1) {
				cal_choice = writable[0];
			} else {
				int pick_sel = 0;
				int pick_ch;

				keypad(win_status, TRUE);

				for (;;) {
					/* compute total width for centering */
					int total_w = 10; /* "Calendar: " */
					int sx;
					for (pi = 0; pi < n_writable; pi++)
						total_w += (int)strlen(st->calendars[writable[pi]].name) + 2;
					total_w += 17; /* "  h/l Enter Esc" */
					sx = (COLS - total_w) / 2;
					if (sx < 1) sx = 1;

					werase(win_status);
					mvwprintw(win_status, 0, sx, "Calendar: ");
					for (pi = 0; pi < n_writable; pi++) {
						int ci = writable[pi];
						int cpair = 32 + ci;
						if (pi == pick_sel) {
							wattron(win_status, COLOR_PAIR(COL_SELECTED) | A_BOLD);
							wprintw(win_status, " %s ", st->calendars[ci].name);
							wattroff(win_status, COLOR_PAIR(COL_SELECTED) | A_BOLD);
						} else {
							wattron(win_status, COLOR_PAIR(cpair));
							wprintw(win_status, " %s ", st->calendars[ci].name);
							wattroff(win_status, COLOR_PAIR(cpair));
						}
					}
					wattron(win_status, COLOR_PAIR(COL_DIM));
					wprintw(win_status, "  h/l Enter Esc");
					wattroff(win_status, COLOR_PAIR(COL_DIM));
					wrefresh(win_status);

					pick_ch = wgetch(win_status);
					if (pick_ch == KEY_LEFT || pick_ch == 'h') {
						if (pick_sel > 0) pick_sel--;
					} else if (pick_ch == KEY_RIGHT || pick_ch == 'l') {
						if (pick_sel < n_writable - 1) pick_sel++;
					} else if (pick_ch == '\n') {
						cal_choice = writable[pick_sel];
						break;
					} else if (pick_ch == 27) {
						goto add_event_done;
					}
				}
			}
		}

		new_ev.cal_idx = cal_choice;
		if (ui_event_editor(st, &new_ev, 1) == 0) {
			if (ical_save_event(&new_ev) == 0) {
				st->need_reload = 1;
				/* upload to CalDAV server if applicable */
				if (cal_choice >= 0 && cal_choice < st->n_calendars &&
				    st->calendars[cal_choice].caldav)
					caldav_put_event(&st->calendars[cal_choice],
					                 new_ev.ics_path);
			}
		}
add_event_done:
		break;
	}

	case KEY_EDIT_EVENT:
		n_day = get_day_events(st, day_events, 256);
		if (n_day > 0 && st->selected_event >= 0 && st->selected_event < n_day) {
			const struct event *sel_ev = day_events[st->selected_event];
			/* block editing subscription (read-only) events */
			if (sel_ev->cal_idx >= 0 && sel_ev->cal_idx < st->n_calendars &&
			    st->calendars[sel_ev->cal_idx].subscription)
				break;
			/* block editing if user is a non-organizer attendee */
			if (sel_ev->organizer_email[0] && sel_ev->n_attendees > 0) {
				int is_attendee = 0, ai;
				const char *me = cal_email(st, sel_ev->cal_idx);
				for (ai = 0; ai < sel_ev->n_attendees; ai++) {
					if (me[0] && strcasecmp(sel_ev->attendees[ai].email, me) == 0) {
						is_attendee = 1;
						break;
					}
				}
				if (is_attendee && strcasecmp(me, sel_ev->organizer_email) != 0)
					break;
			}
			/* find mutable copy */
			int idx;
			for (idx = 0; idx < st->n_events; idx++) {
				if (&st->events[idx] == sel_ev)
					break;
			}
			if (idx < st->n_events) {
				if (ui_event_editor(st, &st->events[idx], 0) == 0) {
					if (ical_save_event(&st->events[idx]) == 0) {
						st->need_reload = 1;
						/* upload to CalDAV server if applicable */
						int ci = st->events[idx].cal_idx;
						if (ci >= 0 && ci < st->n_calendars &&
						    st->calendars[ci].caldav)
							caldav_put_event(&st->calendars[ci],
							                 st->events[idx].ics_path);
					}
				}
			}
		}
		break;

	case KEY_DEL_EVENT:
		n_day = get_day_events(st, day_events, 256);
		if (n_day > 0 && st->selected_event >= 0 && st->selected_event < n_day) {
			const struct event *sel_ev = day_events[st->selected_event];
			/* block deleting subscription (read-only) events */
			if (sel_ev->cal_idx >= 0 && sel_ev->cal_idx < st->n_calendars &&
			    st->calendars[sel_ev->cal_idx].subscription)
				break;
			/* block deleting if user is a non-organizer attendee */
			if (sel_ev->organizer_email[0] && sel_ev->n_attendees > 0) {
				int is_attendee = 0, ai;
				const char *me = cal_email(st, sel_ev->cal_idx);
				for (ai = 0; ai < sel_ev->n_attendees; ai++) {
					if (me[0] && strcasecmp(sel_ev->attendees[ai].email, me) == 0) {
						is_attendee = 1;
						break;
					}
				}
				if (is_attendee && strcasecmp(me, sel_ev->organizer_email) != 0)
					break;
			}
			if (ui_confirm("Delete this event?")) {
				if (ical_delete_event(sel_ev) == 0) {
					st->need_reload = 1;
					/* delete from CalDAV server if applicable */
					int ci = sel_ev->cal_idx;
					if (ci >= 0 && ci < st->n_calendars &&
					    st->calendars[ci].caldav)
						caldav_delete_event(&st->calendars[ci],
						                    sel_ev->ics_path);
				}
			}
		}
		break;

	case KEY_RSVP:
		n_day = get_day_events(st, day_events, 256);
		if (n_day > 0 && st->selected_event >= 0 && st->selected_event < n_day) {
			const struct event *ev = day_events[st->selected_event];
			/* find user's attendee email — try configured, then organizer */
			const char *email = NULL;
			{
				const char *try_emails[3];
				int n_try = 0, ai, ti;
				const char *ce = cal_email(st, ev->cal_idx);
				if (ce[0]) try_emails[n_try++] = ce;
				if (ev->organizer_email[0])
					try_emails[n_try++] = ev->organizer_email;
				for (ti = 0; ti < n_try && !email; ti++) {
					for (ai = 0; ai < ev->n_attendees; ai++) {
						if (strcasecmp(ev->attendees[ai].email,
						               try_emails[ti]) == 0) {
							email = try_emails[ti];
							break;
						}
					}
				}
			}
			if (!email) {
				const char *m = "You are not an attendee of this event";
				int mx = (COLS - (int)strlen(m)) / 2;
				werase(win_status);
				wattron(win_status, COLOR_PAIR(COL_DECLINED));
				mvwprintw(win_status, 0, mx > 1 ? mx : 1, "%s", m);
				wattroff(win_status, COLOR_PAIR(COL_DECLINED));
				wrefresh(win_status);
				wgetch(win_status);
				break;
			}
			if (ev->n_attendees == 0) {
				const char *m = "No attendees on this event";
				int mx = (COLS - (int)strlen(m)) / 2;
				werase(win_status);
				wattron(win_status, COLOR_PAIR(COL_DECLINED));
				mvwprintw(win_status, 0, mx > 1 ? mx : 1, "%s", m);
				wattroff(win_status, COLOR_PAIR(COL_DECLINED));
				wrefresh(win_status);
				wgetch(win_status);
				break;
			}
			int ps = ui_rsvp_prompt();
			if (ps >= 0) {
				if (ical_set_partstat(ev->ics_path, email, ps) == 0) {
					st->need_reload = 1;
					/* upload RSVP change to CalDAV server */
					int ci = ev->cal_idx;
					if (ci >= 0 && ci < st->n_calendars &&
					    st->calendars[ci].caldav)
						caldav_put_event(&st->calendars[ci],
						                 ev->ics_path);
				} else {
					const char *m = "RSVP failed -- your email may not match any attendee";
					int mx = (COLS - (int)strlen(m)) / 2;
					werase(win_status);
					wattron(win_status, COLOR_PAIR(COL_DECLINED));
					mvwprintw(win_status, 0, mx > 1 ? mx : 1, "%s", m);
					wattroff(win_status, COLOR_PAIR(COL_DECLINED));
					wrefresh(win_status);
					wgetch(win_status);
				}
			}
		}
		break;

	case KEY_CAL_MGR:
		if (ui_calendar_manager(st, getenv("HOME")) > 0)
			st->need_reload = 1;
		break;

	case KEY_SYNC: {
		int sync_ret;
		char sync_err[256];

		/* show syncing message */
		werase(win_status);
		wattron(win_status, COLOR_PAIR(COL_DIM));
		mvwprintw(win_status, 0, (COLS - 20) / 2, "Syncing calendars...");
		wattroff(win_status, COLOR_PAIR(COL_DIM));
		wrefresh(win_status);

		sync_ret = vdir_sync_all(st);

		st->need_reload = 1;
		ui_draw(st);

		/* drain any keys typed during sync */
		flushinp();

		/* show result + wait for acknowledgment */
		werase(win_status);
		if (sync_ret == 0) {
			const char *m = "Sync complete — press any key";
			wattron(win_status, COLOR_PAIR(COL_ACCEPTED));
			mvwprintw(win_status, 0, (COLS - (int)strlen(m)) / 2, "%s", m);
			wattroff(win_status, COLOR_PAIR(COL_ACCEPTED));
		} else {
			char sync_msg[300];
			int sync_len;
			if (vdir_sync_error(sync_err, sizeof(sync_err)) > 0)
				sync_len = snprintf(sync_msg, sizeof(sync_msg),
					"Sync failed: %s — press any key", sync_err);
			else
				sync_len = snprintf(sync_msg, sizeof(sync_msg),
					"Sync failed — press any key");
			wattron(win_status, COLOR_PAIR(COL_DECLINED));
			mvwprintw(win_status, 0, (COLS - sync_len) / 2, "%s", sync_msg);
			wattroff(win_status, COLOR_PAIR(COL_DECLINED));
		}
		wrefresh(win_status);
		timeout(-1);
		getch();
		timeout(100);
		break;
	}

	case KEY_QUIT:
		st->running = 0;
		break;

	case KEY_RESIZE:
		/* terminal resized — just redraw */
		break;

	case KEY_F(5):
		/* fewer upcoming days */
		if (st->upcoming_days > 1)
			st->upcoming_days--;
		break;

	case KEY_F(6):
		/* more upcoming days */
		if (st->upcoming_days < 90)
			st->upcoming_days++;
		break;

	default:
		/* 1-8: toggle calendar visibility */
		if (ch >= '1' && ch <= '8') {
			int idx = ch - '1';
			if (idx < st->n_calendars)
				st->calendars[idx].visible = !st->calendars[idx].visible;
		}
		break;
	}

	return 1;
}


/* show a brief message in the detail panel */
static void
detail_msg(const char *msg)
{
	int max_w = getmaxx(win_detail);

	werase(win_detail);
	wattron(win_detail, COLOR_PAIR(COL_BORDER));
	mvwhline(win_detail, 0, 0, ACS_HLINE, max_w);
	wattroff(win_detail, COLOR_PAIR(COL_BORDER));
	wattron(win_detail, COLOR_PAIR(COL_DIM));
	mvwprintw(win_detail, 1, 1, "%s", msg);
	wattroff(win_detail, COLOR_PAIR(COL_DIM));
	wrefresh(win_detail);
}

/* show an error + "press any key" in the detail panel */
static void
detail_error(const char *line1, const char *line2)
{
	int max_w = getmaxx(win_detail);
	int row = 0;

	werase(win_detail);
	wattron(win_detail, COLOR_PAIR(COL_BORDER));
	mvwhline(win_detail, 0, 0, ACS_HLINE, max_w);
	wattroff(win_detail, COLOR_PAIR(COL_BORDER));
	row = 1;

	wattron(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
	mvwprintw(win_detail, row++, 1, "%s", line1);
	wattroff(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
	if (line2) {
		wattron(win_detail, COLOR_PAIR(COL_DIM));
		mvwprintw(win_detail, row++, 1, "%s", line2);
		wattroff(win_detail, COLOR_PAIR(COL_DIM));
	}
	wattron(win_detail, COLOR_PAIR(COL_DIM));
	mvwprintw(win_detail, row + 1, 1, "Press any key.");
	wattroff(win_detail, COLOR_PAIR(COL_DIM));
	wrefresh(win_detail);

	keypad(win_detail, TRUE);
	wtimeout(win_detail, -1);
	wgetch(win_detail);
}

/* render a menu in win_detail, show hint in win_status */
static void
detail_menu_hint(void)
{
	const char *hint = "Esc:cancel";
	int hx = (COLS - (int)strlen(hint)) / 2;

	werase(win_status);
	wattron(win_status, COLOR_PAIR(COL_DIM));
	mvwprintw(win_status, 0, hx > 1 ? hx : 1, "%s", hint);
	wattroff(win_status, COLOR_PAIR(COL_DIM));
	wrefresh(win_status);
}

/* CalDAV calendar picker in win_detail.
 * Returns picked index (0-based), or -1 on cancel. */
static int
detail_cal_picker(char (*names)[MAX_NAME_LEN], int count)
{
	int max_w = getmaxx(win_detail);
	int row = 0;
	int pick, pick_ch;

	werase(win_detail);
	wattron(win_detail, COLOR_PAIR(COL_BORDER));
	mvwhline(win_detail, 0, 0, ACS_HLINE, max_w);
	wattroff(win_detail, COLOR_PAIR(COL_BORDER));
	row = 1;

	wattron(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
	mvwprintw(win_detail, row++, 1, "Select Calendar");
	wattroff(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
	row++;

	for (pick = 0; pick < count && pick < 9; pick++) {
		wattron(win_detail, COLOR_PAIR(COL_ACCENT));
		mvwprintw(win_detail, row, 3, "%d", pick + 1);
		wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
		wprintw(win_detail, "  %s", names[pick]);
		row++;
	}

	detail_menu_hint();
	wrefresh(win_detail);

	keypad(win_detail, TRUE);
	wtimeout(win_detail, -1);
	for (;;) {
		pick_ch = wgetch(win_detail);
		if (pick_ch == 27) return -1;
		if (pick_ch >= '1' && pick_ch <= '9' &&
		    (pick_ch - '1') < count)
			return pick_ch - '1';
	}
}

/* --- calendar manager (inline in events + detail panels) --- */

int
ui_calendar_manager(struct state *st, const char *home)
{
	int sel = 0;
	int changed = 0;
	int ch;

	if (!home)
		return 0;

	timeout(-1); /* blocking input for calendar manager */
	keypad(win_detail, TRUE);

	for (;;) {
		int i, row;
		int ev_h = getmaxy(win_events);
		int ev_w = getmaxx(win_events);
		int det_w = getmaxx(win_detail);

		/* --- draw calendar list in events panel --- */
		werase(win_events);
		wattron(win_events, A_BOLD | COLOR_PAIR(COL_FG));
		mvwprintw(win_events, 0, 1, "Calendars");
		wattroff(win_events, A_BOLD | COLOR_PAIR(COL_FG));

		for (i = 0; i < st->n_calendars; i++) {
			struct calendar *cal = &st->calendars[i];
			int cpair = 32 + i;

			row = i + 2;
			if (row >= ev_h - 1)
				break;

			if (i == sel) {
				wattron(win_events, COLOR_PAIR(COL_SELECTED) | A_BOLD);
				mvwhline(win_events, row, 0, ' ', ev_w);
			}

			mvwprintw(win_events, row, 1, "%s",
			          cal->visible ? "●" : "○");

			if (i != sel)
				wattron(win_events, COLOR_PAIR(cpair) | A_BOLD);
			mvwprintw(win_events, row, 3, "%d: %s",
			          i + 1, cal->name);
			if (i != sel)
				wattroff(win_events, COLOR_PAIR(cpair) | A_BOLD);

			/* type label */
			{
				int tx = ev_w - 14;
				if (tx < 20) tx = 20;
				if (tx > ev_w - 2) tx = ev_w - 2;
				if (i != sel)
					wattron(win_events, COLOR_PAIR(COL_DIM));
				if (cal->subscription)
					mvwprintw(win_events, row, tx, "sub");
				else if (cal->caldav)
					mvwprintw(win_events, row, tx, "dav");
				else
					mvwprintw(win_events, row, tx, "local");
				if (i != sel)
					wattroff(win_events, COLOR_PAIR(COL_DIM));
			}

			if (i == sel)
				wattroff(win_events, COLOR_PAIR(COL_SELECTED) | A_BOLD);
		}
		wrefresh(win_events);

		/* --- draw selected calendar detail --- */
		werase(win_detail);
		wattron(win_detail, COLOR_PAIR(COL_BORDER));
		mvwhline(win_detail, 0, 0, ACS_HLINE, det_w);
		wattroff(win_detail, COLOR_PAIR(COL_BORDER));

		if (sel >= 0 && sel < st->n_calendars) {
			const struct calendar *sc = &st->calendars[sel];
			int drow = 1;

			wattron(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
			mvwprintw(win_detail, drow++, 1, "%s", sc->name);
			wattroff(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));

			wattron(win_detail, COLOR_PAIR(COL_DIM));
			if (sc->subscription) {
				mvwprintw(win_detail, drow++, 1, "subscription");
				if (sc->sub_url[0])
					mvwprintw(win_detail, drow++, 1,
					          "url: %.*s", det_w - 6,
					          sc->sub_url);
			} else if (sc->caldav) {
				mvwprintw(win_detail, drow++, 1, "caldav");
				if (sc->caldav_url[0])
					mvwprintw(win_detail, drow++, 1,
					          "url: %.*s", det_w - 6,
					          sc->caldav_url);
				if (sc->caldav_user[0])
					mvwprintw(win_detail, drow++, 1,
					          "user: %s",
					          sc->caldav_user);
			} else {
				mvwprintw(win_detail, drow++, 1, "local");
			}
			if (sc->email[0])
				mvwprintw(win_detail, drow++, 1,
				          "email: %s", sc->email);
			mvwprintw(win_detail, drow++, 1,
			          "path: %.*s", det_w - 7, sc->path);
			wattroff(win_detail, COLOR_PAIR(COL_DIM));
		}
		wrefresh(win_detail);

		/* --- draw status hints (centered) --- */
		{
			const char *hint;
			int is_local = (sel >= 0 && sel < st->n_calendars &&
			                !st->calendars[sel].caldav &&
			                !st->calendars[sel].subscription);
			int hx;

			if (is_local)
				hint = "q:back  a:add  e:edit  d:caldav  x:del  F5/F6:reorder  Ctrl-R:sync";
			else
				hint = "q:back  a:add  e:edit  x:del  F5/F6:reorder  Ctrl-R:sync";
			hx = (COLS - (int)strlen(hint)) / 2;
			werase(win_status);
			wattron(win_status, COLOR_PAIR(COL_DIM));
			mvwprintw(win_status, 0, hx > 1 ? hx : 1,
			          "%s", hint);
			wattroff(win_status, COLOR_PAIR(COL_DIM));
			wrefresh(win_status);
		}

		ch = getch();

		if (ch == 'q' || ch == 27)
			break;
		if (ch == KEY_RESIZE)
			continue;

		switch (ch) {
		case KEY_CAL_UP:
		case KEY_UP:
			if (sel > 0) sel--;
			break;

		case KEY_CAL_DOWN:
		case KEY_DOWN:
			if (sel < st->n_calendars - 1) sel++;
			break;

		case 'a': {
			/* --- add calendar --- */
			char name_buf[MAX_NAME_LEN] = {0};
			struct form_field afields[6];
			int anf, type_ch;

			/* step 1: get name */
			anf = 0;
			afields[anf].label = "Name: ";
			afields[anf].buf = name_buf;
			afields[anf].maxlen = MAX_NAME_LEN;
			afields[anf].readonly = 0;
			afields[anf].password = 0;
			afields[anf].required = 1;
			afields[anf].multiline = 0;
			afields[anf].validate = validate_cal_name;
			anf++;

			if (form_run(win_detail, afields, anf, 0, 1,
			             "Add Calendar") != 0)
				break;

			/* step 2: type selection in detail panel */
			{
				int drow = 0;

				werase(win_detail);
				wattron(win_detail, COLOR_PAIR(COL_BORDER));
				mvwhline(win_detail, 0, 0, ACS_HLINE, det_w);
				wattroff(win_detail, COLOR_PAIR(COL_BORDER));
				drow = 1;

				wattron(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));
				mvwprintw(win_detail, drow++, 1, "Add Calendar");
				wattroff(win_detail, A_BOLD | COLOR_PAIR(COL_ACCENT));

				wattron(win_detail, COLOR_PAIR(COL_DIM));
				mvwprintw(win_detail, drow, 1, "Name: ");
				wattroff(win_detail, COLOR_PAIR(COL_DIM));
				wprintw(win_detail, "%s", name_buf);
				drow += 2;

				wattron(win_detail, COLOR_PAIR(COL_DIM));
				mvwprintw(win_detail, drow++, 1, "Type:");
				wattroff(win_detail, COLOR_PAIR(COL_DIM));

				wattron(win_detail, COLOR_PAIR(COL_ACCENT));
				mvwprintw(win_detail, drow, 3, "l");
				wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
				wprintw(win_detail, "  local calendar");
				drow++;

				wattron(win_detail, COLOR_PAIR(COL_ACCENT));
				mvwprintw(win_detail, drow, 3, "s");
				wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
				wprintw(win_detail, "  ICS subscription");
				drow++;

				wattron(win_detail, COLOR_PAIR(COL_ACCENT));
				mvwprintw(win_detail, drow, 3, "c");
				wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
				wprintw(win_detail, "  CalDAV");

				detail_menu_hint();
				wrefresh(win_detail);
			}

			wtimeout(win_detail, -1);
			for (;;) {
				type_ch = wgetch(win_detail);
				if (type_ch == 's' || type_ch == 'S' ||
				    type_ch == 'c' || type_ch == 'C' ||
				    type_ch == 'l' || type_ch == 'L' ||
				    type_ch == 27)
					break;
			}

			if (type_ch == 27)
				break;

			if (type_ch == 'l' || type_ch == 'L') {
				vdir_add_local_calendar(home, st, name_buf);
				changed = 1;
			} else if (type_ch == 's' || type_ch == 'S') {
				/* ICS subscription — needs a URL */
				char url_buf[MAX_PATH_LEN] = {0};

				anf = 0;
				afields[anf].label = "ICS URL: ";
				afields[anf].buf = url_buf;
				afields[anf].maxlen = MAX_PATH_LEN;
				afields[anf].readonly = 0;
				afields[anf].password = 0;
				afields[anf].required = 1;
				afields[anf].multiline = 0;
				afields[anf].validate = NULL;
				anf++;

				if (form_run(win_detail, afields, anf, 0, 1,
				             "Add ICS Subscription") != 0)
					break;

				vdir_add_subscription(home, st, name_buf,
				                      url_buf);

				{
					char msg[256];
					snprintf(msg, sizeof(msg),
					         "Fetching %s...", name_buf);
					detail_msg(msg);
				}

				def_prog_mode();
				endwin();
				vdir_fetch_subscription(
					&st->calendars[st->n_calendars - 1]);
				reset_prog_mode();
				refresh();
				draw_header(NULL);
				draw_sidebar(st);
				draw_month_cal(st);
				changed = 1;
			} else if (type_ch == 'c' || type_ch == 'C') {
				/* CalDAV */
				char url_buf[MAX_PATH_LEN] = {0};
				char user_buf[MAX_NAME_LEN] = {0};
				char pass_buf[2048] = {0};
				int prov_ch;

				/* provider selection */
				{
					int drow = 0;

					werase(win_detail);
					wattron(win_detail, COLOR_PAIR(COL_BORDER));
					mvwhline(win_detail, 0, 0, ACS_HLINE,
					         det_w);
					wattroff(win_detail, COLOR_PAIR(COL_BORDER));
					drow = 1;

					wattron(win_detail,
					        A_BOLD | COLOR_PAIR(COL_ACCENT));
					mvwprintw(win_detail, drow++, 1,
					          "Add CalDAV Calendar");
					wattroff(win_detail,
					         A_BOLD | COLOR_PAIR(COL_ACCENT));

					wattron(win_detail, COLOR_PAIR(COL_DIM));
					mvwprintw(win_detail, drow, 1, "Name: ");
					wattroff(win_detail, COLOR_PAIR(COL_DIM));
					wprintw(win_detail, "%s", name_buf);
					drow += 2;

					wattron(win_detail, COLOR_PAIR(COL_DIM));
					mvwprintw(win_detail, drow++, 1,
					          "Provider:");
					wattroff(win_detail, COLOR_PAIR(COL_DIM));

					wattron(win_detail, COLOR_PAIR(COL_ACCENT));
					mvwprintw(win_detail, drow, 3, "g");
					wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
					wprintw(win_detail, "  Google");
					drow++;

					wattron(win_detail, COLOR_PAIR(COL_ACCENT));
					mvwprintw(win_detail, drow, 3, "i");
					wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
					wprintw(win_detail, "  iCloud");
					drow++;

					wattron(win_detail, COLOR_PAIR(COL_ACCENT));
					mvwprintw(win_detail, drow, 3, "c");
					wattroff(win_detail, COLOR_PAIR(COL_ACCENT));
					wprintw(win_detail, "  Custom URL");

					detail_menu_hint();
					wrefresh(win_detail);
				}

				wtimeout(win_detail, -1);
				for (;;) {
					prov_ch = wgetch(win_detail);
					if (prov_ch == 'g' || prov_ch == 'G' ||
					    prov_ch == 'i' || prov_ch == 'I' ||
					    prov_ch == 'c' || prov_ch == 'C' ||
					    prov_ch == 27)
						break;
				}

				if (prov_ch == 27)
					break;

				{
				int is_oauth = 0;

				if (prov_ch == 'g' || prov_ch == 'G') {
					/* Google OAuth2 */
					char gid[512] = {0};
					char gsecret[512] = {0};

					if (goauth_load_client(home, gid,
					    sizeof(gid), gsecret,
					    sizeof(gsecret)) != 0) {
						/* need to configure OAuth client */
						char id_buf[512] = {0};
						char secret_buf[512] = {0};

						anf = 0;
						afields[anf].label = "Client ID: ";
						afields[anf].buf = id_buf;
						afields[anf].maxlen = sizeof(id_buf);
						afields[anf].readonly = 0;
						afields[anf].password = 0;
						afields[anf].required = 1;
						afields[anf].multiline = 0;
						afields[anf].validate = NULL;
						anf++;
						afields[anf].label = "Client Secret: ";
						afields[anf].buf = secret_buf;
						afields[anf].maxlen = sizeof(secret_buf);
						afields[anf].readonly = 0;
						afields[anf].password = 1;
						afields[anf].required = 1;
						afields[anf].multiline = 0;
						afields[anf].validate = NULL;
						anf++;

						if (form_run(win_detail, afields,
						             anf, 0, 1,
						             "Google OAuth Setup") != 0)
							break;

						goauth_save_client(home,
						    id_buf, secret_buf);
						strncpy(gid, id_buf, sizeof(gid) - 1);
						strncpy(gsecret, secret_buf,
						        sizeof(gsecret) - 1);
						memset(id_buf, 0, sizeof(id_buf));
						memset(secret_buf, 0,
						       sizeof(secret_buf));
					}

					/* ask for email */
					anf = 0;
					afields[anf].label = "Google email: ";
					afields[anf].buf = user_buf;
					afields[anf].maxlen = MAX_NAME_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 0;
					afields[anf].required = 1;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;

					if (form_run(win_detail, afields, anf,
					             0, 1,
					             "Add Google Calendar") != 0) {
						memset(gid, 0, sizeof(gid));
						memset(gsecret, 0, sizeof(gsecret));
						break;
					}

					detail_msg(
					    "Waiting for Google sign-in...");
					doupdate();

					/* run OAuth flow (opens browser) */
					if (goauth_authorize(home, name_buf,
					    gid, gsecret) != 0) {
						memset(gid, 0, sizeof(gid));
						memset(gsecret, 0, sizeof(gsecret));
						detail_error(
						    "Authorization failed.",
						    "Try again or check credentials.");
						break;
					}

					memset(gid, 0, sizeof(gid));
					memset(gsecret, 0, sizeof(gsecret));

					/* get token for discovery */
					if (goauth_get_token(home, name_buf,
					    pass_buf,
					    sizeof(pass_buf)) != 0) {
						detail_error(
						    "Failed to get token.",
						    "Try again.");
						break;
					}

					snprintf(url_buf, sizeof(url_buf),
					    "https://apidata.googleusercontent.com"
					    "/caldav/v2/");
					is_oauth = 1;

				} else if (prov_ch == 'i' || prov_ch == 'I') {
					char hint_buf[128];
					snprintf(hint_buf, sizeof(hint_buf),
					         "Generate at appleid.apple.com > App-Specific Passwords");

					anf = 0;
					afields[anf].label = "Note: ";
					afields[anf].buf = hint_buf;
					afields[anf].maxlen = sizeof(hint_buf);
					afields[anf].readonly = 1;
					afields[anf].password = 0;
					afields[anf].required = 0;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;
					afields[anf].label = "Apple ID email: ";
					afields[anf].buf = user_buf;
					afields[anf].maxlen = MAX_NAME_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 0;
					afields[anf].required = 1;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;
					afields[anf].label = "App password: ";
					afields[anf].buf = pass_buf;
					afields[anf].maxlen = MAX_NAME_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 1;
					afields[anf].required = 1;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;

					if (form_run(win_detail, afields, anf,
					             0, 1,
					             "Add iCloud Calendar") != 0)
						break;

					snprintf(url_buf, sizeof(url_buf),
					         "https://caldav.icloud.com/");
				} else {
					/* Custom URL */
					anf = 0;
					afields[anf].label = "CalDAV URL: ";
					afields[anf].buf = url_buf;
					afields[anf].maxlen = MAX_PATH_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 0;
					afields[anf].required = 1;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;
					afields[anf].label = "Username: ";
					afields[anf].buf = user_buf;
					afields[anf].maxlen = MAX_NAME_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 0;
					afields[anf].required = 0;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;
					afields[anf].label = "Password: ";
					afields[anf].buf = pass_buf;
					afields[anf].maxlen = MAX_NAME_LEN;
					afields[anf].readonly = 0;
					afields[anf].password = 1;
					afields[anf].required = 0;
					afields[anf].multiline = 0;
					afields[anf].validate = NULL;
					anf++;

					if (form_run(win_detail, afields, anf,
					             0, 1,
					             "Add CalDAV Calendar") != 0)
						break;
				}

				/* discover calendars from the base URL */
				{
					char disc_names[16][MAX_NAME_LEN];
					char disc_urls[16][MAX_PATH_LEN];
					int disc_count;

					detail_msg("Discovering calendars...");

					memset(disc_names, 0, sizeof(disc_names));
					memset(disc_urls, 0, sizeof(disc_urls));
					disc_count = caldav_discover(url_buf,
					             user_buf, pass_buf,
					             disc_names, disc_urls,
					             16, is_oauth);

					if (disc_count <= 0) {
						detail_error(
							"Failed to connect.",
							"Check URL and credentials.");
					} else if (disc_count == 1) {
						if (is_oauth)
							vdir_add_oauth_calendar(
							    home, st, name_buf,
							    disc_urls[0],
							    user_buf);
						else
							vdir_add_caldav_calendar(
							    home, st, name_buf,
							    disc_urls[0],
							    user_buf, pass_buf);
						{
							char msg[256];
							snprintf(msg, sizeof(msg),
							         "Syncing %s...",
							         name_buf);
							detail_msg(msg);
						}
						caldav_sync_calendar(
							&st->calendars[
							st->n_calendars - 1]);
						changed = 1;
					} else {
						/* multiple — let user pick */
						int idx = detail_cal_picker(
							disc_names, disc_count);
						if (idx >= 0) {
							if (is_oauth)
								vdir_add_oauth_calendar(
								    home, st,
								    name_buf,
								    disc_urls[idx],
								    user_buf);
							else
								vdir_add_caldav_calendar(
								    home, st,
								    name_buf,
								    disc_urls[idx],
								    user_buf,
								    pass_buf);
							{
								char msg[256];
								snprintf(msg,
								         sizeof(msg),
								         "Syncing %s...",
								         name_buf);
								detail_msg(msg);
							}
							caldav_sync_calendar(
								&st->calendars[
								st->n_calendars - 1]);
							changed = 1;
						}
					}
				}
				} /* end is_oauth scope */
			}
			break;
		}

		case 'x':
			if (st->n_calendars <= 1)
				break;
			if (sel >= 0 && sel < st->n_calendars) {
				if (ui_confirm("Remove this calendar?")) {
					if (st->calendars[sel].caldav)
						vdir_remove_secret(home,
						         st->calendars[sel].name);
					vdir_remove_calendar(home, st, sel);
					if (sel >= st->n_calendars && sel > 0)
						sel--;
					changed = 1;
				}
			}
			break;

		case 'e': {
			/* edit calendar settings */
			struct calendar *cal;
			struct form_field ef[FORM_MAX_FIELDS];
			int enf = 0;
			char edit_title[MAX_NAME_LEN + 16];
			char name_buf[MAX_NAME_LEN] = {0};
			char email_buf[MAX_EMAIL_LEN] = {0};
			char url_buf[MAX_PATH_LEN] = {0};
			char user_buf[MAX_NAME_LEN] = {0};
			char pass_buf[MAX_NAME_LEN] = {0};
			char type_label[16];

			if (sel < 0 || sel >= st->n_calendars)
				break;
			cal = &st->calendars[sel];

			snprintf(edit_title, sizeof(edit_title),
			         "Edit: %s", cal->name);

			/* type (readonly) */
			strncpy(type_label,
			        cal->subscription ? "subscription" :
			        cal->caldav ? "caldav" : "local",
			        sizeof(type_label) - 1);
			ef[enf].label = "Type: ";
			ef[enf].buf = type_label;
			ef[enf].maxlen = sizeof(type_label);
			ef[enf].readonly = 1;
			ef[enf].password = 0;
			ef[enf].required = 0;
			ef[enf].multiline = 0;
			ef[enf].validate = NULL;
			enf++;

			/* name */
			strncpy(name_buf, cal->name, MAX_NAME_LEN - 1);
			ef[enf].label = "Name: ";
			ef[enf].buf = name_buf;
			ef[enf].maxlen = MAX_NAME_LEN;
			ef[enf].readonly = 0;
			ef[enf].password = 0;
			ef[enf].required = 1;
			ef[enf].multiline = 0;
			ef[enf].validate = NULL;
			enf++;

			/* subscription fields */
			if (cal->subscription) {
				strncpy(url_buf, cal->sub_url,
				        MAX_PATH_LEN - 1);
				ef[enf].label = "ICS URL: ";
				ef[enf].buf = url_buf;
				ef[enf].maxlen = MAX_PATH_LEN;
				ef[enf].readonly = 0;
				ef[enf].password = 0;
				ef[enf].required = 1;
				ef[enf].multiline = 0;
				ef[enf].validate = NULL;
				enf++;
			}

			/* caldav fields */
			if (cal->caldav) {
				strncpy(url_buf, cal->caldav_url,
				        MAX_PATH_LEN - 1);
				ef[enf].label = "CalDAV URL: ";
				ef[enf].buf = url_buf;
				ef[enf].maxlen = MAX_PATH_LEN;
				ef[enf].readonly = 0;
				ef[enf].password = 0;
				ef[enf].required = 1;
				ef[enf].multiline = 0;
				ef[enf].validate = NULL;
				enf++;

				if (cal->caldav_user[0])
					strncpy(user_buf, cal->caldav_user,
					        MAX_NAME_LEN - 1);
				ef[enf].label = "Username: ";
				ef[enf].buf = user_buf;
				ef[enf].maxlen = MAX_NAME_LEN;
				ef[enf].readonly = 0;
				ef[enf].password = 0;
				ef[enf].required = 0;
				ef[enf].multiline = 0;
				ef[enf].validate = NULL;
				enf++;

				ef[enf].label = "Password (blank=keep): ";
				ef[enf].buf = pass_buf;
				ef[enf].maxlen = MAX_NAME_LEN;
				ef[enf].readonly = 0;
				ef[enf].password = 1;
				ef[enf].required = 0;
				ef[enf].multiline = 0;
				ef[enf].validate = NULL;
				enf++;
			}

			/* email (only for local/caldav) */
			if (!cal->subscription) {
				if (cal->email[0])
					strncpy(email_buf, cal->email,
					        MAX_EMAIL_LEN - 1);
				ef[enf].label = "RSVP email: ";
				ef[enf].buf = email_buf;
				ef[enf].maxlen = MAX_EMAIL_LEN;
				ef[enf].readonly = 0;
				ef[enf].password = 0;
				ef[enf].required = 0;
				ef[enf].multiline = 0;
				ef[enf].validate = NULL;
				enf++;
			}

			if (form_run(win_detail, ef, enf, 0, 1,
			             edit_title) == 0) {
				if (name_buf[0]) {
					strncpy(cal->name, name_buf,
					        MAX_NAME_LEN - 1);
					cal->name[MAX_NAME_LEN - 1] = '\0';
					sanitize_name(cal->name, MAX_NAME_LEN);
				}

				if (cal->subscription) {
					strncpy(cal->sub_url, url_buf,
					        MAX_PATH_LEN - 1);
					cal->sub_url[MAX_PATH_LEN - 1] = '\0';
				}

				if (cal->caldav) {
					char old_pass[MAX_NAME_LEN] = {0};

					strncpy(cal->caldav_user, user_buf,
					        MAX_NAME_LEN - 1);
					if (pass_buf[0])
						vdir_update_secret(home,
						         cal->name, pass_buf);

					/* re-discover to let user pick */
					{
						char disc_names[16][MAX_NAME_LEN];
						char disc_urls[16][MAX_PATH_LEN];
						int disc_count;
						const char *disc_pass;

						if (pass_buf[0]) {
							disc_pass = pass_buf;
						} else {
							read_secret(home,
							    cal->name, old_pass,
							    sizeof(old_pass));
							disc_pass = old_pass;
						}

						detail_msg(
						    "Discovering calendars...");

						memset(disc_names, 0,
						       sizeof(disc_names));
						memset(disc_urls, 0,
						       sizeof(disc_urls));
						disc_count = caldav_discover(
						    url_buf[0] ? url_buf
						              : cal->caldav_url,
						    cal->caldav_user, disc_pass,
						    disc_names, disc_urls, 16,
						    cal->oauth);

						if (disc_count > 1) {
							int idx =
							    detail_cal_picker(
							        disc_names,
							        disc_count);
							if (idx >= 0)
								strncpy(
								    cal->caldav_url,
								    disc_urls[idx],
								    MAX_PATH_LEN - 1);
						} else if (disc_count == 1) {
							strncpy(cal->caldav_url,
							        disc_urls[0],
							        MAX_PATH_LEN - 1);
						} else {
							detail_error(
							    "Failed to connect.",
							    "Check credentials.");
						}

						memset(old_pass, 0,
						       sizeof(old_pass));
					}

					vdir_save_config(home, st);

					/* sync after changes */
					{
						char msg[256];
						snprintf(msg, sizeof(msg),
						         "Syncing %s...",
						         cal->name);
						detail_msg(msg);
					}
					caldav_sync_calendar(cal);
				}

				if (!cal->subscription) {
					strncpy(cal->email, email_buf,
					        MAX_EMAIL_LEN - 1);
					cal->email[MAX_EMAIL_LEN - 1] = '\0';
				}

				vdir_save_config(home, st);
				changed = 1;
			}
			break;
		}

		case 'd': {
			/* enable CalDAV on a local calendar */
			struct calendar *cal;
			struct form_field df[3];
			int dnf = 0;
			char url_buf[MAX_PATH_LEN] = {0};
			char user_buf[MAX_NAME_LEN] = {0};
			char pass_buf[MAX_NAME_LEN] = {0};

			if (sel < 0 || sel >= st->n_calendars)
				break;
			cal = &st->calendars[sel];
			if (cal->caldav || cal->subscription)
				break;

			df[dnf].label = "CalDAV URL: ";
			df[dnf].buf = url_buf;
			df[dnf].maxlen = MAX_PATH_LEN;
			df[dnf].readonly = 0;
			df[dnf].password = 0;
			df[dnf].required = 1;
			df[dnf].multiline = 0;
			df[dnf].validate = NULL;
			dnf++;
			df[dnf].label = "Username: ";
			df[dnf].buf = user_buf;
			df[dnf].maxlen = MAX_NAME_LEN;
			df[dnf].readonly = 0;
			df[dnf].password = 0;
			df[dnf].required = 0;
			df[dnf].multiline = 0;
			df[dnf].validate = NULL;
			dnf++;
			df[dnf].label = "Password: ";
			df[dnf].buf = pass_buf;
			df[dnf].maxlen = MAX_NAME_LEN;
			df[dnf].readonly = 0;
			df[dnf].password = 1;
			df[dnf].required = 0;
			df[dnf].multiline = 0;
			df[dnf].validate = NULL;
			dnf++;

			if (form_run(win_detail, df, dnf, 0, 1,
			             "Enable CalDAV sync") == 0) {
				cal->caldav = 1;
				strncpy(cal->caldav_url, url_buf,
				        MAX_PATH_LEN - 1);
				strncpy(cal->caldav_user, user_buf,
				        MAX_NAME_LEN - 1);

				if (pass_buf[0])
					vdir_update_secret(home,
					         cal->name, pass_buf);

				vdir_save_config(home, st);

				{
					char msg[256];
					snprintf(msg, sizeof(msg),
					         "Discovering and syncing %s...",
					         cal->name);
					detail_msg(msg);
				}
				caldav_sync_calendar(cal);
				changed = 1;
			}
			break;
		}

		case KEY_F(5):
			/* move calendar up */
			if (sel > 0) {
				struct calendar tmp = st->calendars[sel];
				st->calendars[sel] = st->calendars[sel - 1];
				st->calendars[sel - 1] = tmp;
				sel--;
				vdir_save_config(home, st);
				changed = 1;
			}
			break;

		case KEY_F(6):
			/* move calendar down */
			if (sel < st->n_calendars - 1) {
				struct calendar tmp = st->calendars[sel];
				st->calendars[sel] = st->calendars[sel + 1];
				st->calendars[sel + 1] = tmp;
				sel++;
				vdir_save_config(home, st);
				changed = 1;
			}
			break;

		case 18: { /* Ctrl-R */
			int sync_ret;
			char sync_err[256];

			/* show syncing message */
			werase(win_status);
			wattron(win_status, COLOR_PAIR(COL_DIM));
			mvwprintw(win_status, 0, (COLS - 20) / 2,
			          "Syncing calendars...");
			wattroff(win_status, COLOR_PAIR(COL_DIM));
			wrefresh(win_status);

			sync_ret = vdir_sync_all(st);

			/* drain keys typed during sync */
			flushinp();

			/* show result + wait for acknowledgment */
			werase(win_status);
			if (sync_ret == 0) {
				const char *m = "Sync complete — press any key";
				wattron(win_status, COLOR_PAIR(COL_ACCEPTED));
				mvwprintw(win_status, 0,
				          (COLS - (int)strlen(m)) / 2,
				          "%s", m);
				wattroff(win_status, COLOR_PAIR(COL_ACCEPTED));
			} else {
				char sync_msg[300];
				int sync_len;
				if (vdir_sync_error(sync_err,
				                    sizeof(sync_err)) > 0)
					sync_len = snprintf(sync_msg,
					    sizeof(sync_msg),
					    "Sync failed: %s — press any key",
					    sync_err);
				else
					sync_len = snprintf(sync_msg,
					    sizeof(sync_msg),
					    "Sync failed — press any key");
				wattron(win_status, COLOR_PAIR(COL_DECLINED));
				mvwprintw(win_status, 0,
				          (COLS - sync_len) / 2,
				          "%s", sync_msg);
				wattroff(win_status, COLOR_PAIR(COL_DECLINED));
			}
			wrefresh(win_status);
			getch(); /* cal manager already in blocking mode */
			changed = 1;
			break;
		}
		}
	}

	timeout(100); /* restore main loop timeout */
	return changed;
}
