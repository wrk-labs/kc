/* See LICENSE file for copyright and license details. */
#ifndef KC_H
#define KC_H

#include <time.h>

/* limits */
#define MAX_CALENDARS   8
#define MAX_EVENTS      4096
#define MAX_ATTENDEES   64
#define MAX_PATH_LEN    1024
#define MAX_SUMMARY_LEN 256
#define MAX_NAME_LEN    128
#define MAX_EMAIL_LEN   256
#define MAX_LOCATION_LEN 256

/* participation status */
enum partstat {
	PARTSTAT_NEEDSACTION,
	PARTSTAT_ACCEPTED,
	PARTSTAT_DECLINED,
	PARTSTAT_TENTATIVE,
};

/* event status (iCalendar STATUS property) */
enum event_status {
	STATUS_NONE,
	STATUS_CONFIRMED,
	STATUS_TENTATIVE,
	STATUS_CANCELLED,
};

/* recurrence frequency */
enum recur_freq {
	RECUR_NONE,
	RECUR_DAILY,
	RECUR_WEEKLY,
	RECUR_MONTHLY,
	RECUR_YEARLY,
};

/* calendar view */
enum view {
	VIEW_MONTH,
};

/* attendee */
struct attendee {
	char name[MAX_NAME_LEN];
	char email[MAX_EMAIL_LEN];
	enum partstat status;
	int rsvp;
};

/* event */
struct event {
	char uid[MAX_SUMMARY_LEN];
	char summary[MAX_SUMMARY_LEN];
	char description[MAX_SUMMARY_LEN * 4];
	char location[MAX_LOCATION_LEN];
	time_t start;
	time_t end;
	int all_day;

	/* status */
	enum event_status status;

	/* recurrence */
	enum recur_freq recur_freq;
	int recur_interval;        /* e.g. every 2 weeks */
	int recur_count;           /* 0 = no limit */
	time_t recur_until;        /* 0 = no end date */
	int is_recurrence;         /* 1 = expanded instance, not the original */

	/* organizer */
	char organizer_name[MAX_NAME_LEN];
	char organizer_email[MAX_EMAIL_LEN];

	/* attendees */
	struct attendee attendees[MAX_ATTENDEES];
	int n_attendees;

	/* source */
	char ics_path[MAX_PATH_LEN];
	int cal_idx;  /* index into calendars[] */
};

/* calendar source */
struct calendar {
	char name[MAX_NAME_LEN];
	char path[MAX_PATH_LEN];     /* vdir path */
	short color;                  /* color pair index */
	int visible;
	int caldav;                   /* 1 = CalDAV sync enabled */
	char caldav_url[MAX_PATH_LEN];
	char caldav_user[MAX_NAME_LEN];
	int oauth;                    /* 1 = Google OAuth2 auth */
	int subscription;            /* 1 = read-only .ics URL subscription */
	char sub_url[MAX_PATH_LEN];  /* .ics subscription URL */
	char email[MAX_EMAIL_LEN];   /* RSVP email (overrides global) */
};

/* app state */
struct state {
	/* calendars */
	struct calendar calendars[MAX_CALENDARS];
	int n_calendars;

	/* events (loaded from all visible calendars) */
	struct event events[MAX_EVENTS];
	int n_events;

	/* navigation */
	struct tm cursor;            /* currently selected date */
	struct tm today;             /* today's date (set at startup) */
	enum view view;
	int selected_event;          /* index into day's event list, -1 = none */

	/* ui state */
	int running;
	int need_reload;
	int upcoming_days;           /* upcoming section: show next N days (0 = all) */
	int detail_scroll;           /* detail panel scroll offset */

	/* user identity (for RSVP — matched against ATTENDEE emails) */
	char user_email[MAX_EMAIL_LEN];
};

/* sanitize.c — input sanitization */
void  sanitize_str(char *s);
void  sanitize_name(char *s, size_t maxlen);
int   is_safe_name(const char *s);

/* cal.c — date utilities */
int   cal_days_in_month(int year, int month);
int   cal_dow(int year, int month, int day);
int   cal_same_day(const struct tm *a, const struct tm *b);
void  cal_today(struct tm *t);
void  cal_next_day(struct tm *t);
void  cal_prev_day(struct tm *t);
void  cal_next_week(struct tm *t);
void  cal_prev_week(struct tm *t);
void  cal_next_month(struct tm *t);
void  cal_prev_month(struct tm *t);
void  cal_set_today(struct tm *t);

/* ical.c — libical integration */
int   ical_load_file(const char *path, int cal_idx, struct event *events,
                     int max_events, int cur_count);
int   ical_remove_cancelled(struct event *events, int n);
int   ical_save_event(const struct event *ev);
int   ical_delete_event(const struct event *ev);
int   ical_set_partstat(const char *ics_path, const char *email,
                        enum partstat status);
int   ical_add_attendee(const char *ics_path, const char *email,
                         const char *name);
int   ical_remove_attendee(const char *ics_path, const char *email);
const char *partstat_str(enum partstat s);
const char *event_status_str(enum event_status s);
const char *recur_freq_str(enum recur_freq f);

/* vdir.c — vdir filesystem operations */
int   vdir_load_calendar(const char *path, int cal_idx,
                         struct event *events, int max_events, int cur_count);
int   vdir_init_data_dir(const char *home);
int   vdir_load_config(const char *home, struct state *st);
int   vdir_save_config(const char *home, const struct state *st);
char *vdir_new_ics_path(const char *cal_path);
int   vdir_add_local_calendar(const char *home, struct state *st,
                              const char *name);
int   vdir_add_caldav_calendar(const char *home, struct state *st,
                               const char *name, const char *url,
                               const char *username, const char *password);
int   vdir_add_oauth_calendar(const char *home, struct state *st,
                               const char *name, const char *url,
                               const char *username);
int   vdir_remove_calendar(const char *home, struct state *st, int idx);
int   vdir_update_secret(const char *home, const char *name,
                          const char *password);
int   vdir_remove_secret(const char *home, const char *name);
int   vdir_sync_error(char *buf, size_t buflen);
int   vdir_add_subscription(const char *home, struct state *st,
                             const char *name, const char *url);
int   vdir_fetch_subscription(const struct calendar *cal);
int   vdir_sync_all(const struct state *st);

/* when nonzero, sync and OAuth flows print progress to stderr.
 * set by main() before pre-TUI sync; cleared before ncurses init so
 * in-TUI sync paths (which have their own status bar) stay silent. */
extern int kc_progress_verbose;

/* caldav.c — native CalDAV sync */
int   read_secret(const char *home, const char *name, char *pass, size_t passlen);
int   caldav_discover(const char *base_url, const char *user, const char *pass,
                      char (*names)[MAX_NAME_LEN], char (*urls)[MAX_PATH_LEN],
                      int max_results, int bearer);
int   caldav_sync_calendar(const struct calendar *cal);
int   caldav_put_event(const struct calendar *cal, const char *ics_path);
int   caldav_delete_event(const struct calendar *cal, const char *ics_path);

/* goauth.c — Google OAuth2 */
int   goauth_get_token(const char *home, const char *cal_name,
                        char *token, size_t tokenlen);
int   goauth_authorize(const char *home, const char *cal_name,
                        const char *client_id, const char *client_secret);
int   goauth_load_client(const char *home, char *id, size_t idlen,
                          char *secret, size_t secretlen);
int   goauth_save_client(const char *home, const char *id, const char *secret);
int   goauth_remove_tokens(const char *home, const char *cal_name);

/* ui.c — ncurses interface */
void  ui_init(void);
void  ui_cleanup(void);
void  ui_draw(struct state *st);
int   ui_input(struct state *st);
int   ui_event_editor(struct state *st, struct event *ev, int is_new);
int   ui_confirm(const char *msg);
int   ui_rsvp_prompt(void);
int   ui_calendar_manager(struct state *st, const char *home);

#endif /* KC_H */
