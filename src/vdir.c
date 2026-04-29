/* See LICENSE file for copyright and license details. */
/* vdir.c — vdir filesystem operations and config management */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

#include "kc.h"
#include "config.h"

int kc_progress_verbose = 0;

/* helper — not exposed in header */
static void
copy_str(char *dst, const char *src, size_t n)
{
	strncpy(dst, src, n - 1);
	dst[n - 1] = '\0';
}

int
vdir_load_calendar(const char *path, int cal_idx,
                   struct event *events, int max_events, int cur_count)
{
	DIR *dir;
	struct dirent *ent;
	char filepath[MAX_PATH_LEN];
	int count = cur_count;

	dir = opendir(path);
	if (!dir)
		return cur_count;

	while ((ent = readdir(dir)) != NULL && count < max_events) {
		size_t len = strlen(ent->d_name);
		if (len < 5 || strcmp(ent->d_name + len - 4, ".ics") != 0)
			continue;

		snprintf(filepath, sizeof(filepath), "%s/%s", path, ent->d_name);
		count = ical_load_file(filepath, cal_idx, events, max_events, count);
	}

	closedir(dir);
	return count;
}

int
vdir_init_data_dir(const char *home)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s", home, data_dir);
	mkdir(path, 0755);

	snprintf(path, sizeof(path), "%s/%s/calendars", home, data_dir);
	mkdir(path, 0755);


	return 0;
}

/*
 * config file format (~/.kc/config):
 *
 * email user@example.com
 *
 * calendar <name>
 *   path <vdir-path>
 *   color <0-7>
 *   readonly
 *   caldav <url>
 *   caldav_user <username>
 *
 * Passwords are stored in ~/.kc/secrets.
 */
int
vdir_load_config(const char *home, struct state *st)
{
	char path[MAX_PATH_LEN];
	FILE *fp;
	char line[1024];
	struct calendar *cal = NULL;

	snprintf(path, sizeof(path), "%s/%s/config", home, data_dir);

	fp = fopen(path, "r");
	if (!fp) {
		/* no config — create default local calendar */
		st->n_calendars = 1;
		cal = &st->calendars[0];
		memset(cal, 0, sizeof(*cal));
		copy_str(cal->name, "default", MAX_NAME_LEN);
		snprintf(cal->path, MAX_PATH_LEN,
		         "%s/%s/calendars/default", home, data_dir);
		mkdir(cal->path, 0755);
		cal->color = cal_colors[0];
		cal->visible = 1;

		/* save it so the file exists next time */
		vdir_save_config(home, st);
		return 0;
	}

	st->n_calendars = 0;

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';

		/* skip empty lines and comments */
		if (line[0] == '#' || line[0] == '\0')
			continue;

		if (strncmp(line, "email ", 6) == 0) {
			copy_str(st->user_email, line + 6, MAX_EMAIL_LEN);

		} else if (strncmp(line, "calendar ", 9) == 0) {
			if (st->n_calendars >= MAX_CALENDARS)
				continue;
			cal = &st->calendars[st->n_calendars];
			memset(cal, 0, sizeof(*cal));
			copy_str(cal->name, line + 9, MAX_NAME_LEN);
			sanitize_name(cal->name, MAX_NAME_LEN);
			cal->color = cal_colors[st->n_calendars % 8];
			cal->visible = 1;
			st->n_calendars++;

		} else if (cal) {
			/* indented lines belong to current calendar */
			const char *p = line;
			while (*p == ' ' || *p == '\t') p++;

			if (strncmp(p, "path ", 5) == 0) {
				copy_str(cal->path, p + 5, MAX_PATH_LEN);
				/* reject absolute paths and ".." to prevent traversal */
				if (strstr(cal->path, "..") || cal->path[0] == '/') {
					snprintf(cal->path, MAX_PATH_LEN,
					         "%s/%s/calendars/%s", home, data_dir, cal->name);
				}
			} else if (strncmp(p, "color ", 6) == 0) {
				int c = atoi(p + 6);
				cal->color = (c >= 0 && c < 8) ? cal_colors[c] : cal_colors[0];
			} else if (strncmp(p, "subscription ", 13) == 0) {
				cal->subscription = 1;
				copy_str(cal->sub_url, p + 13, MAX_PATH_LEN);
			} else if (strncmp(p, "caldav ", 7) == 0) {
				cal->caldav = 1;
				copy_str(cal->caldav_url, p + 7, MAX_PATH_LEN);
			} else if (strncmp(p, "caldav_user ", 12) == 0) {
				copy_str(cal->caldav_user, p + 12, MAX_NAME_LEN);
			} else if (strcmp(p, "oauth") == 0) {
				cal->oauth = 1;
			} else if (strncmp(p, "email ", 6) == 0) {
				copy_str(cal->email, p + 6, MAX_EMAIL_LEN);
			}
		}
	}

	fclose(fp);
	return 0;
}

int
vdir_save_config(const char *home, const struct state *st)
{
	char path[MAX_PATH_LEN];
	FILE *fp;
	int i;

	snprintf(path, sizeof(path), "%s/%s/config", home, data_dir);

	fp = fopen(path, "w");
	if (!fp)
		return -1;

	fprintf(fp, "# kc configuration\n");
	fprintf(fp, "# edit with care — or use 'c' in kc to manage calendars\n\n");

	if (st->user_email[0])
		fprintf(fp, "email %s\n\n", st->user_email);

	for (i = 0; i < st->n_calendars; i++) {
		const struct calendar *c = &st->calendars[i];
		int ci = 0, j;

		for (j = 0; j < 8; j++) {
			if (cal_colors[j] == c->color) {
				ci = j;
				break;
			}
		}

		fprintf(fp, "calendar %s\n", c->name);
		fprintf(fp, "  path %s\n", c->path);
		fprintf(fp, "  color %d\n", ci);
		if (c->email[0])
			fprintf(fp, "  email %s\n", c->email);
		if (c->subscription)
			fprintf(fp, "  subscription %s\n", c->sub_url);
		if (c->caldav) {
			fprintf(fp, "  caldav %s\n", c->caldav_url);
			if (c->caldav_user[0])
				fprintf(fp, "  caldav_user %s\n", c->caldav_user);
			if (c->oauth)
				fprintf(fp, "  oauth\n");
		}
		fprintf(fp, "\n");
	}

	fclose(fp);
	return 0;
}

int
vdir_add_local_calendar(const char *home, struct state *st, const char *name)
{
	struct calendar *cal;

	if (st->n_calendars >= MAX_CALENDARS)
		return -1;

	cal = &st->calendars[st->n_calendars];
	memset(cal, 0, sizeof(*cal));
	copy_str(cal->name, name, MAX_NAME_LEN);
	sanitize_name(cal->name, MAX_NAME_LEN);
	snprintf(cal->path, MAX_PATH_LEN,
	         "%s/%s/calendars/%s", home, data_dir, cal->name);
	mkdir(cal->path, 0755);
	cal->color = cal_colors[st->n_calendars % 8];
	cal->visible = 1;
	st->n_calendars++;

	vdir_save_config(home, st);
	return 0;
}

int
vdir_add_caldav_calendar(const char *home, struct state *st,
                         const char *name, const char *url,
                         const char *username, const char *password)
{
	struct calendar *cal;

	if (st->n_calendars >= MAX_CALENDARS)
		return -1;

	cal = &st->calendars[st->n_calendars];
	memset(cal, 0, sizeof(*cal));
	copy_str(cal->name, name, MAX_NAME_LEN);
	sanitize_name(cal->name, MAX_NAME_LEN);
	snprintf(cal->path, MAX_PATH_LEN,
	         "%s/%s/calendars/%s", home, data_dir, cal->name);
	mkdir(cal->path, 0755);
	cal->color = cal_colors[st->n_calendars % 8];
	cal->visible = 1;
	cal->caldav = 1;
	copy_str(cal->caldav_url, url, MAX_PATH_LEN);
	copy_str(cal->caldav_user, username, MAX_NAME_LEN);
	st->n_calendars++;

	vdir_save_config(home, st);

	/* store password in secrets file (use sanitized name) */
	vdir_update_secret(home, cal->name, password);

	return 0;
}

int
vdir_add_oauth_calendar(const char *home, struct state *st,
                         const char *name, const char *url,
                         const char *username)
{
	struct calendar *cal;

	if (st->n_calendars >= MAX_CALENDARS)
		return -1;

	cal = &st->calendars[st->n_calendars];
	memset(cal, 0, sizeof(*cal));
	copy_str(cal->name, name, MAX_NAME_LEN);
	sanitize_name(cal->name, MAX_NAME_LEN);
	snprintf(cal->path, MAX_PATH_LEN,
	         "%s/%s/calendars/%s", home, data_dir, cal->name);
	mkdir(cal->path, 0755);
	cal->color = cal_colors[st->n_calendars % 8];
	cal->visible = 1;
	cal->caldav = 1;
	cal->oauth = 1;
	copy_str(cal->caldav_url, url, MAX_PATH_LEN);
	copy_str(cal->caldav_user, username, MAX_NAME_LEN);
	st->n_calendars++;

	vdir_save_config(home, st);
	return 0;
}

int
vdir_remove_calendar(const char *home, struct state *st, int idx)
{
	int i;
	char path[MAX_PATH_LEN * 2];

	if (idx < 0 || idx >= st->n_calendars)
		return -1;

	/* remove calendar data directory (ics files, sync state) */
	if (st->calendars[idx].path[0]) {
		struct stat sb;
		/* refuse to follow symlinks */
		if (lstat(st->calendars[idx].path, &sb) == 0 &&
		    S_ISDIR(sb.st_mode) && !S_ISLNK(sb.st_mode)) {
			DIR *dir = opendir(st->calendars[idx].path);
			if (dir) {
				struct dirent *ent;
				while ((ent = readdir(dir)) != NULL) {
					if (ent->d_name[0] == '.' &&
					    (ent->d_name[1] == '\0' ||
					     (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
						continue;
					snprintf(path, sizeof(path), "%s/%s",
					         st->calendars[idx].path, ent->d_name);
					/* skip symlinks inside the directory too */
					if (lstat(path, &sb) == 0 && S_ISLNK(sb.st_mode))
						continue;
					unlink(path);
				}
				closedir(dir);
				rmdir(st->calendars[idx].path);
			}
		}
	}

	/* remove OAuth tokens if applicable */
	if (st->calendars[idx].oauth)
		goauth_remove_tokens(home, st->calendars[idx].name);

	/* remove secret entry — rewrite secrets file without this calendar */
	{
		char sec_path[MAX_PATH_LEN];
		char tmp_path[MAX_PATH_LEN];
		FILE *fp, *tmp;
		char line[2048];

		snprintf(sec_path, sizeof(sec_path), "%s/.kc/secrets", home);
		snprintf(tmp_path, sizeof(tmp_path), "%s/.kc/secrets.tmp", home);
		fp = fopen(sec_path, "r");
		tmp = fopen(tmp_path, "w");
		if (fp && tmp) {
			size_t nlen = strlen(st->calendars[idx].name);
			while (fgets(line, sizeof(line), fp)) {
				/* skip lines starting with this calendar name */
				if (strncmp(line, st->calendars[idx].name, nlen) == 0 &&
				    (line[nlen] == ' ' || line[nlen] == '\t' ||
				     line[nlen] == '\n' || line[nlen] == '\0'))
					continue;
				fputs(line, tmp);
			}
			fclose(fp);
			fclose(tmp);
			rename(tmp_path, sec_path);
		} else {
			if (fp) fclose(fp);
			if (tmp) fclose(tmp);
		}
	}

	/* shift remaining calendars down */
	for (i = idx; i < st->n_calendars - 1; i++)
		st->calendars[i] = st->calendars[i + 1];
	st->n_calendars--;

	vdir_save_config(home, st);

	return 0;
}


char *
vdir_new_ics_path(const char *cal_path)
{
	static char path[MAX_PATH_LEN];
	char uid[64];
	unsigned char rnd[16];
	FILE *fp;
	struct timespec ts;

	/* try /dev/urandom for strong randomness */
	fp = fopen("/dev/urandom", "r");
	if (fp && fread(rnd, 1, sizeof(rnd), fp) == sizeof(rnd)) {
		fclose(fp);
		snprintf(uid, sizeof(uid),
		         "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
		         "%02x%02x-%02x%02x%02x%02x%02x%02x",
		         rnd[0], rnd[1], rnd[2], rnd[3],
		         rnd[4], rnd[5], rnd[6], rnd[7],
		         rnd[8], rnd[9], rnd[10], rnd[11],
		         rnd[12], rnd[13], rnd[14], rnd[15]);
	} else {
		/* fallback: time + nanoseconds + pid */
		if (fp) fclose(fp);
		clock_gettime(CLOCK_REALTIME, &ts);
		snprintf(uid, sizeof(uid), "%lx-%lx-%x",
		         (long)ts.tv_sec, (long)ts.tv_nsec, getpid());
	}

	snprintf(path, sizeof(path), "%s/%s.ics", cal_path, uid);
	return path;
}


/* read first line of sync log for error feedback */
int
vdir_sync_error(char *buf, size_t buflen)
{
	char path[MAX_PATH_LEN];
	const char *home = getenv("HOME");
	FILE *fp;

	if (!home)
		return -1;

	snprintf(path, sizeof(path), "%s/%s/sync.log",
	         home, data_dir);

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	buf[0] = '\0';
	if (fgets(buf, (int)buflen, fp)) {
		buf[strcspn(buf, "\n")] = '\0';
	}
	fclose(fp);

	return buf[0] ? 1 : 0;
}

int
vdir_update_secret(const char *home, const char *name, const char *password)
{
	char spath[MAX_PATH_LEN];
	char tpath[MAX_PATH_LEN];
	FILE *fp, *tp;
	char line[1024];
	int prefix_len = (int)strlen(name);

	snprintf(spath, sizeof(spath), "%s/%s/secrets",
	         home, data_dir);
	snprintf(tpath, sizeof(tpath), "%s/%s/secrets.tmp",
	         home, data_dir);

	fp = fopen(spath, "r");
	{
		int fd = open(tpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			if (fp) fclose(fp);
			return -1;
		}
		tp = fdopen(fd, "w");
		if (!tp) {
			close(fd);
			if (fp) fclose(fp);
			return -1;
		}
	}

	/* copy all lines except the one matching this calendar name */
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, name, prefix_len) == 0 && line[prefix_len] == ' ')
				continue; /* skip old entry */
			fputs(line, tp);
		}
		fclose(fp);
	}

	/* append new entry */
	fprintf(tp, "%s %s\n", name, password);
	memset(line, 0, sizeof(line)); /* clear sensitive data */
	fclose(tp);
	rename(tpath, spath);

	return 0;
}

int
vdir_add_subscription(const char *home, struct state *st,
                       const char *name, const char *url)
{
	struct calendar *cal;

	if (st->n_calendars >= MAX_CALENDARS)
		return -1;

	cal = &st->calendars[st->n_calendars];
	memset(cal, 0, sizeof(*cal));
	copy_str(cal->name, name, MAX_NAME_LEN);
	sanitize_name(cal->name, MAX_NAME_LEN);
	snprintf(cal->path, MAX_PATH_LEN,
	         "%s/%s/calendars/%s", home, data_dir, cal->name);
	mkdir(cal->path, 0755);
	cal->color = cal_colors[st->n_calendars % 8];
	cal->visible = 1;
	cal->subscription = 1;
	copy_str(cal->sub_url, url, MAX_PATH_LEN);
	st->n_calendars++;

	vdir_save_config(home, st);
	return 0;
}

/*
 * Fetch a subscription .ics URL and store it in the calendar directory.
 * Uses libcurl directly (no shell). The entire feed is stored as a single
 * feed.ics file which our loader handles (multiple VEVENTs per file).
 *
 * Returns 0 on success, -1 on failure.
 */
int
vdir_fetch_subscription(const struct calendar *cal)
{
	char dest[MAX_PATH_LEN + 16];
	CURL *curl;
	CURLcode res;
	FILE *fp;

	if (!cal->subscription || !cal->sub_url[0])
		return -1;

	/* only allow http/https URLs */
	if (strncmp(cal->sub_url, "https://", 8) != 0 &&
	    strncmp(cal->sub_url, "http://", 7) != 0)
		return -1;

	snprintf(dest, sizeof(dest), "%s/feed.ics", cal->path);

	fp = fopen(dest, "wb");
	if (!fp)
		return -1;

	curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, cal->sub_url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); /* auto-decompress */
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "kc/" VERSION);
#ifdef CURLOPT_PROTOCOLS_STR
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		unlink(dest); /* remove partial download */
		return -1;
	}

	return 0;
}

/*
 * Sync all calendars: fetch subscriptions + native CalDAV sync.
 * Returns 0 if everything succeeded, -1 if any sync failed.
 */
int
vdir_sync_all(const struct state *st)
{
	int i, rc, ret = 0;
	const struct calendar *cal;

	/* fetch all .ics subscriptions */
	for (i = 0; i < st->n_calendars; i++) {
		cal = &st->calendars[i];
		if (!cal->subscription || !cal->visible)
			continue;
		if (kc_progress_verbose) {
			fprintf(stderr, "kc: fetching %s... ", cal->name);
			fflush(stderr);
		}
		rc = vdir_fetch_subscription(cal);
		if (kc_progress_verbose)
			fprintf(stderr, "%s\n", rc == 0 ? "ok" : "failed");
		if (rc != 0)
			ret = -1;
	}

	/* sync CalDAV calendars using native libcurl */
	for (i = 0; i < st->n_calendars; i++) {
		cal = &st->calendars[i];
		if (!cal->caldav || !cal->visible)
			continue;
		if (kc_progress_verbose) {
			fprintf(stderr, "kc: syncing %s... ", cal->name);
			fflush(stderr);
		}
		rc = caldav_sync_calendar(cal);
		if (kc_progress_verbose)
			fprintf(stderr, "%s\n", rc == 0 ? "ok" : "failed");
		if (rc != 0)
			ret = -1;
	}

	return ret;
}

int
vdir_remove_secret(const char *home, const char *name)
{
	char spath[MAX_PATH_LEN];
	char tpath[MAX_PATH_LEN];
	FILE *fp, *tp;
	char line[1024];
	int prefix_len = (int)strlen(name);

	snprintf(spath, sizeof(spath), "%s/%s/secrets",
	         home, data_dir);
	snprintf(tpath, sizeof(tpath), "%s/%s/secrets.tmp",
	         home, data_dir);

	fp = fopen(spath, "r");
	if (!fp)
		return 0; /* nothing to remove */

	{
		int fd = open(tpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (fd < 0) {
			fclose(fp);
			return -1;
		}
		tp = fdopen(fd, "w");
		if (!tp) {
			close(fd);
			fclose(fp);
			return -1;
		}
	}

	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, name, prefix_len) == 0 && line[prefix_len] == ' ')
			continue; /* skip matching entry */
		fputs(line, tp);
	}

	memset(line, 0, sizeof(line));
	fclose(fp);
	fclose(tp);
	rename(tpath, spath);

	return 0;
}
