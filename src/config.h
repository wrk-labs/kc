/* See LICENSE file for copyright and license details. */
/* kc - kai calendar configuration */

/* appearance */
static const char *font = "Fira Code:size=10";

/* Kai Dark Gray + Orange theme */
#define COL_BG         0   /* #161616 — background */
#define COL_FG         1   /* #c8c8c8 — foreground */
#define COL_ACCENT     2   /* #e38735 — orange accent */
#define COL_DIM        3   /* #555555 — dimmed text */
#define COL_TODAY      4   /* #e38735 on #2a2a2a — today highlight */
#define COL_SELECTED   5   /* #161616 on #e38735 — selected item */
#define COL_BORDER     6   /* #333333 — panel borders */
#define COL_ACCEPTED   7   /* #6ab050 — accepted */
#define COL_DECLINED   8   /* #e85555 — declined */
#define COL_TENTATIVE  9   /* #5c8dcc — tentative */
#define COL_NEEDSACTION 10 /* #9664dc — needs action */

/* calendar colors (up to 8 calendars) */
static const short cal_colors[] = {
	/* index into ncurses color pairs — assigned at runtime */
	2,  /* calendar 1: orange */
	4,  /* calendar 2: blue */
	5,  /* calendar 3: magenta */
	3,  /* calendar 4: green */
	6,  /* calendar 5: cyan */
	1,  /* calendar 6: red */
	7,  /* calendar 7: white */
	3,  /* calendar 8: yellow */
};

/*
 * keybindings
 *
 * arrows : event list navigation
 * h/j/k/l: calendar grid navigation (vim-style)
 */

/* calendar grid (vim-style, inverted j/k to match Kai's dwm) */
#define KEY_CAL_UP    'j'
#define KEY_CAL_DOWN  'k'
#define KEY_CAL_LEFT  'h'
#define KEY_CAL_RIGHT 'l'

#define KEY_PREV_MONTH 'H'
#define KEY_NEXT_MONTH 'L'
#define KEY_TODAY       'o'

#define KEY_ADD_EVENT  'a'
#define KEY_EDIT_EVENT 'e'
#define KEY_DEL_EVENT  'x'
#define KEY_RSVP       'r'
#define KEY_SYNC       18   /* Ctrl-R */
#define KEY_TOGGLE_CAL '1'   /* 1-8 toggle calendar visibility */
#define KEY_CAL_MGR    'c'   /* open calendar manager */

#define KEY_QUIT       'q'

/* data directory (relative to $HOME) */
static const char *data_dir = ".kc";

/* date/time formats */
static const char *date_fmt = "%a %b %d, %Y";
static const char *time_fmt = "%H:%M";

/* first day of week: 0=Sunday, 1=Monday */
static const int first_dow = 1;

/* sync interval hint (seconds) — used by kai-daemon, not kc itself */
static const int sync_interval = 300;
