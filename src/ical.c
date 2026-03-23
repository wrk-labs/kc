/* See LICENSE file for copyright and license details. */
/* ical.c — libical integration for reading/writing .ics files */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libical/ical.h>

#include "kc.h"

static void
copy_truncate(char *dst, const char *src, size_t n)
{
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, n - 1);
	dst[n - 1] = '\0';
}

static enum partstat
ical_to_partstat(icalparameter_partstat ps)
{
	switch (ps) {
	case ICAL_PARTSTAT_ACCEPTED:  return PARTSTAT_ACCEPTED;
	case ICAL_PARTSTAT_DECLINED:  return PARTSTAT_DECLINED;
	case ICAL_PARTSTAT_TENTATIVE: return PARTSTAT_TENTATIVE;
	default:                      return PARTSTAT_NEEDSACTION;
	}
}

static icalparameter_partstat
partstat_to_ical(enum partstat s)
{
	switch (s) {
	case PARTSTAT_ACCEPTED:    return ICAL_PARTSTAT_ACCEPTED;
	case PARTSTAT_DECLINED:    return ICAL_PARTSTAT_DECLINED;
	case PARTSTAT_TENTATIVE:   return ICAL_PARTSTAT_TENTATIVE;
	case PARTSTAT_NEEDSACTION: return ICAL_PARTSTAT_NEEDSACTION;
	}
	return ICAL_PARTSTAT_NEEDSACTION;
}

const char *
partstat_str(enum partstat s)
{
	switch (s) {
	case PARTSTAT_ACCEPTED:    return "accepted";
	case PARTSTAT_DECLINED:    return "declined";
	case PARTSTAT_TENTATIVE:   return "tentative";
	case PARTSTAT_NEEDSACTION: return "needs action";
	}
	return "unknown";
}

const char *
event_status_str(enum event_status s)
{
	switch (s) {
	case STATUS_CONFIRMED:  return "confirmed";
	case STATUS_TENTATIVE:  return "tentative";
	case STATUS_CANCELLED:  return "cancelled";
	case STATUS_NONE:       return "";
	}
	return "";
}

const char *
recur_freq_str(enum recur_freq f)
{
	switch (f) {
	case RECUR_DAILY:   return "daily";
	case RECUR_WEEKLY:  return "weekly";
	case RECUR_MONTHLY: return "monthly";
	case RECUR_YEARLY:  return "yearly";
	case RECUR_NONE:    return "";
	}
	return "";
}

static enum recur_freq
ical_to_recur_freq(icalrecurrencetype_frequency f)
{
	switch (f) {
	case ICAL_DAILY_RECURRENCE:   return RECUR_DAILY;
	case ICAL_WEEKLY_RECURRENCE:  return RECUR_WEEKLY;
	case ICAL_MONTHLY_RECURRENCE: return RECUR_MONTHLY;
	case ICAL_YEARLY_RECURRENCE:  return RECUR_YEARLY;
	default:                      return RECUR_NONE;
	}
}

static icalrecurrencetype_frequency
recur_freq_to_ical(enum recur_freq f)
{
	switch (f) {
	case RECUR_DAILY:   return ICAL_DAILY_RECURRENCE;
	case RECUR_WEEKLY:  return ICAL_WEEKLY_RECURRENCE;
	case RECUR_MONTHLY: return ICAL_MONTHLY_RECURRENCE;
	case RECUR_YEARLY:  return ICAL_YEARLY_RECURRENCE;
	case RECUR_NONE:    return ICAL_NO_RECURRENCE;
	}
	return ICAL_NO_RECURRENCE;
}

static enum event_status
ical_to_event_status(icalproperty_status s)
{
	switch (s) {
	case ICAL_STATUS_CONFIRMED: return STATUS_CONFIRMED;
	case ICAL_STATUS_TENTATIVE: return STATUS_TENTATIVE;
	case ICAL_STATUS_CANCELLED: return STATUS_CANCELLED;
	default:                    return STATUS_NONE;
	}
}

static icalproperty_status
event_status_to_ical(enum event_status s)
{
	switch (s) {
	case STATUS_CONFIRMED: return ICAL_STATUS_CONFIRMED;
	case STATUS_TENTATIVE: return ICAL_STATUS_TENTATIVE;
	case STATUS_CANCELLED: return ICAL_STATUS_CANCELLED;
	case STATUS_NONE:      return ICAL_STATUS_NONE;
	}
	return ICAL_STATUS_NONE;
}

static void
parse_attendees(icalcomponent *vevent, struct event *ev)
{
	icalproperty *prop;
	const char *val;
	icalparameter *param;

	ev->n_attendees = 0;

	for (prop = icalcomponent_get_first_property(vevent, ICAL_ATTENDEE_PROPERTY);
	     prop && ev->n_attendees < MAX_ATTENDEES;
	     prop = icalcomponent_get_next_property(vevent, ICAL_ATTENDEE_PROPERTY)) {

		struct attendee *att = &ev->attendees[ev->n_attendees];
		memset(att, 0, sizeof(*att));

		/* email (strip mailto:) */
		val = icalproperty_get_attendee(prop);
		if (val) {
			if (strncasecmp(val, "mailto:", 7) == 0)
				val += 7;
			copy_truncate(att->email, val, MAX_EMAIL_LEN);
			sanitize_str(att->email);
		}

		/* display name */
		param = icalproperty_get_first_parameter(prop, ICAL_CN_PARAMETER);
		if (param) {
			copy_truncate(att->name, icalparameter_get_cn(param), MAX_NAME_LEN);
			sanitize_str(att->name);
		}

		/* participation status */
		param = icalproperty_get_first_parameter(prop, ICAL_PARTSTAT_PARAMETER);
		if (param)
			att->status = ical_to_partstat(icalparameter_get_partstat(param));
		else
			att->status = PARTSTAT_NEEDSACTION;

		/* rsvp */
		param = icalproperty_get_first_parameter(prop, ICAL_RSVP_PARAMETER);
		att->rsvp = (param && icalparameter_get_rsvp(param) == ICAL_RSVP_TRUE);

		ev->n_attendees++;
	}
}

static void
parse_organizer(icalcomponent *vevent, struct event *ev)
{
	icalproperty *prop;
	const char *val;
	icalparameter *param;

	prop = icalcomponent_get_first_property(vevent, ICAL_ORGANIZER_PROPERTY);
	if (!prop)
		return;

	val = icalproperty_get_organizer(prop);
	if (val) {
		if (strncasecmp(val, "mailto:", 7) == 0)
			val += 7;
		copy_truncate(ev->organizer_email, val, MAX_EMAIL_LEN);
		sanitize_str(ev->organizer_email);
	}

	param = icalproperty_get_first_parameter(prop, ICAL_CN_PARAMETER);
	if (param) {
		copy_truncate(ev->organizer_name, icalparameter_get_cn(param), MAX_NAME_LEN);
		sanitize_str(ev->organizer_name);
	}
}

static int
parse_vevent(icalcomponent *vevent, const char *path, int cal_idx,
             struct event *ev)
{
	icalproperty *prop;
	struct icaltimetype dtstart, dtend;

	memset(ev, 0, sizeof(*ev));

	/* UID */
	prop = icalcomponent_get_first_property(vevent, ICAL_UID_PROPERTY);
	if (prop)
		copy_truncate(ev->uid, icalproperty_get_uid(prop), MAX_SUMMARY_LEN);
	sanitize_str(ev->uid);

	/* summary */
	prop = icalcomponent_get_first_property(vevent, ICAL_SUMMARY_PROPERTY);
	if (prop)
		copy_truncate(ev->summary, icalproperty_get_summary(prop), MAX_SUMMARY_LEN);
	sanitize_str(ev->summary);

	/* description */
	prop = icalcomponent_get_first_property(vevent, ICAL_DESCRIPTION_PROPERTY);
	if (prop)
		copy_truncate(ev->description, icalproperty_get_description(prop),
		              sizeof(ev->description));
	sanitize_str(ev->description);

	/* location */
	prop = icalcomponent_get_first_property(vevent, ICAL_LOCATION_PROPERTY);
	if (prop)
		copy_truncate(ev->location, icalproperty_get_location(prop), MAX_LOCATION_LEN);
	sanitize_str(ev->location);

	/* start time */
	dtstart = icalcomponent_get_dtstart(vevent);
	if (icaltime_is_null_time(dtstart))
		return -1;

	ev->all_day = dtstart.is_date;
	ev->start = icaltime_as_timet_with_zone(dtstart, icaltimezone_get_utc_timezone());

	/* end time */
	dtend = icalcomponent_get_dtend(vevent);
	if (!icaltime_is_null_time(dtend))
		ev->end = icaltime_as_timet_with_zone(dtend, icaltimezone_get_utc_timezone());
	else
		ev->end = ev->start + 3600; /* default 1h */

	/* status */
	prop = icalcomponent_get_first_property(vevent, ICAL_STATUS_PROPERTY);
	if (prop)
		ev->status = ical_to_event_status(icalproperty_get_status(prop));
	else
		ev->status = STATUS_NONE;

	/* recurrence (RRULE) */
	prop = icalcomponent_get_first_property(vevent, ICAL_RRULE_PROPERTY);
	if (prop) {
		struct icalrecurrencetype rrule = icalproperty_get_rrule(prop);
		ev->recur_freq = ical_to_recur_freq(rrule.freq);
		ev->recur_interval = rrule.interval > 0 ? rrule.interval : 1;
		ev->recur_count = rrule.count;
		if (!icaltime_is_null_time(rrule.until))
			ev->recur_until = icaltime_as_timet_with_zone(
				rrule.until, icaltimezone_get_utc_timezone());
	}

	/* organizer + attendees */
	parse_organizer(vevent, ev);
	parse_attendees(vevent, ev);

	/* source tracking */
	copy_truncate(ev->ics_path, path, MAX_PATH_LEN);
	ev->cal_idx = cal_idx;

	return 0;
}

int
ical_load_file(const char *path, int cal_idx, struct event *events,
               int max_events, int cur_count)
{
	FILE *fp;
	long len;
	char *buf;
	icalcomponent *root, *vevent;
	int count = cur_count;

	/*
	 * Collect all VEVENT components into an array before processing.
	 * This avoids iterator corruption: the inner RECURRENCE-ID scan
	 * used to call icalcomponent_get_first_component() on root,
	 * which reset the outer loop's iterator and silently dropped
	 * remaining VEVENTs (e.g. non-cancelled overrides).
	 */
	icalcomponent *vevents[512];
	int n_vevents = 0;

	fp = fopen(path, "r");
	if (!fp)
		return cur_count;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len <= 0 || len > 1024 * 1024) { /* skip files > 1MB */
		fclose(fp);
		return cur_count;
	}

	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return cur_count;
	}

	if ((long)fread(buf, 1, len, fp) != len) {
		free(buf);
		fclose(fp);
		return cur_count;
	}
	buf[len] = '\0';
	fclose(fp);

	root = icalparser_parse_string(buf);
	free(buf);

	if (!root)
		return cur_count;

	/* collect all VEVENTs into array (safe from iterator resets) */
	for (vevent = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
	     vevent && n_vevents < 512;
	     vevent = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
		vevents[n_vevents++] = vevent;
	}

	{
		int vi;
		for (vi = 0; vi < n_vevents && count < max_events; vi++) {
			vevent = vevents[vi];

			if (parse_vevent(vevent, path, cal_idx, &events[count]) != 0)
				continue;

			/*
			 * Cancelled overrides (deleted recurrence instances):
			 * keep them in the events array so cross-file
			 * deduplication can match them against RRULE-expanded
			 * instances from other .ics files with the same UID.
			 * ical_remove_cancelled() strips them after all files
			 * are loaded.
			 */
			if (icalcomponent_get_first_property(vevent,
			    ICAL_RECURRENCEID_PROPERTY)) {
				events[count].is_recurrence = 1;
				count++;
				continue;
			}

			/* expand recurring events */
			if (events[count].recur_freq != RECUR_NONE) {
				struct event base = events[count];
				icalproperty *rrule_prop, *exd_prop;
				struct icalrecurrencetype rrule;
				icalrecur_iterator *ritr;
				struct icaltimetype dtstart, next;
				time_t duration = base.end - base.start;
				time_t now = time(NULL);
				time_t window_start = now - 365 * 86400;
				time_t window_end = now + 365 * 86400;
				int max_instances = 500;
				int inst = 0;

				/* collect EXDATE values (excluded dates) */
				time_t exdates[256];
				int n_exdates = 0;
				for (exd_prop = icalcomponent_get_first_property(
					vevent, ICAL_EXDATE_PROPERTY);
				     exd_prop && n_exdates < 256;
				     exd_prop = icalcomponent_get_next_property(
					vevent, ICAL_EXDATE_PROPERTY)) {
					struct icaltimetype exdt =
						icalproperty_get_exdate(exd_prop);
					if (!icaltime_is_null_time(exdt))
						exdates[n_exdates++] =
							icaltime_as_timet_with_zone(
								exdt,
								icaltimezone_get_utc_timezone());
				}

				/* also exclude dates with RECURRENCE-ID overrides
				 * (sibling VEVENTs within this file) */
				{
					int si;
					for (si = 0; si < n_vevents && n_exdates < 256; si++) {
						icalproperty *rid = icalcomponent_get_first_property(
							vevents[si], ICAL_RECURRENCEID_PROPERTY);
						if (!rid)
							continue;
						struct icaltimetype ridt =
							icalproperty_get_recurrenceid(rid);
						if (!icaltime_is_null_time(ridt))
							exdates[n_exdates++] =
								icaltime_as_timet_with_zone(
									ridt,
									icaltimezone_get_utc_timezone());
					}
				}

				rrule_prop = icalcomponent_get_first_property(
					vevent, ICAL_RRULE_PROPERTY);
				if (!rrule_prop) {
					count++;
					continue;
				}

				rrule = icalproperty_get_rrule(rrule_prop);
				dtstart = icalcomponent_get_dtstart(vevent);
				ritr = icalrecur_iterator_new(rrule, dtstart);
				if (!ritr) {
					count++;
					continue;
				}

				/* first occurrence */
				next = icalrecur_iterator_next(ritr);
				{
					/* check if base occurrence is excluded */
					time_t bt = base.start;
					int excluded = 0, ei;
					for (ei = 0; ei < n_exdates; ei++) {
						if (bt == exdates[ei] ||
						    (base.all_day &&
						     bt / 86400 == exdates[ei] / 86400)) {
							excluded = 1;
							break;
						}
					}
					if (!excluded)
						count++; /* keep base event */
				}

				while (!icaltime_is_null_time(
					next = icalrecur_iterator_next(ritr))
				       && count < max_events
				       && inst < max_instances) {
					time_t t = icaltime_as_timet_with_zone(
						next, icaltimezone_get_utc_timezone());
					if (t > window_end)
						break;
					if (t >= window_start) {
						/* check EXDATE exclusion */
						int excluded = 0, ei;
						for (ei = 0; ei < n_exdates; ei++) {
							if (t == exdates[ei] ||
							    (base.all_day &&
							     t / 86400 == exdates[ei] / 86400)) {
								excluded = 1;
								break;
							}
						}
						if (!excluded) {
							events[count] = base;
							events[count].start = t;
							events[count].end = t + duration;
							events[count].is_recurrence = 1;
							count++;
						}
					}
					inst++;
				}

				icalrecur_iterator_free(ritr);
				continue;
			}

			count++;
		}
	}

	icalcomponent_free(root);
	return count;
}

/*
 * Remove cancelled recurrence overrides and their matching
 * RRULE-expanded instances from the events array.
 *
 * This handles the cross-file case: when a cancelled VEVENT
 * (RECURRENCE-ID + STATUS:CANCELLED) is in a separate .ics file
 * from the master RRULE event, the RRULE expansion can't exclude
 * it at load time. This post-processing step matches them by UID
 * and start time, removing both the cancelled marker and the
 * duplicate expanded instance.
 *
 * Call after all .ics files have been loaded.
 */
int
ical_remove_cancelled(struct event *events, int n)
{
	int i, j, out;

	/* pass 1: for each cancelled override, mark matching expanded instances */
	for (i = 0; i < n; i++) {
		if (events[i].status != STATUS_CANCELLED || !events[i].is_recurrence)
			continue;
		for (j = 0; j < n; j++) {
			if (j == i)
				continue;
			if (events[j].status == STATUS_CANCELLED)
				continue;
			if (strcmp(events[j].uid, events[i].uid) != 0)
				continue;
			/* match by start time (same day for all-day events) */
			if (events[j].start == events[i].start ||
			    (events[j].all_day &&
			     events[j].start / 86400 == events[i].start / 86400)) {
				events[j].status = STATUS_CANCELLED;
			}
		}
	}

	/* pass 2: compact — remove all cancelled events */
	out = 0;
	for (i = 0; i < n; i++) {
		if (events[i].status == STATUS_CANCELLED)
			continue;
		if (out != i)
			events[out] = events[i];
		out++;
	}

	return out;
}

int
ical_set_partstat(const char *ics_path, const char *email, enum partstat status)
{
	FILE *fp;
	long len;
	char *buf;
	icalcomponent *root, *vevent;
	icalproperty *prop;
	icalparameter *param;
	const char *val;
	int found = 0;

	/* read file */
	fp = fopen(ics_path, "r");
	if (!fp)
		return -1;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len <= 0 || len > 1024 * 1024) { /* reject files > 1MB */
		fclose(fp);
		return -1;
	}

	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return -1;
	}

	if ((long)fread(buf, 1, len, fp) != len) {
		free(buf);
		fclose(fp);
		return -1;
	}
	buf[len] = '\0';
	fclose(fp);

	root = icalparser_parse_string(buf);
	free(buf);

	if (!root)
		return -1;

	/* find the ATTENDEE matching our email and update PARTSTAT */
	for (vevent = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
	     vevent;
	     vevent = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {

		for (prop = icalcomponent_get_first_property(vevent, ICAL_ATTENDEE_PROPERTY);
		     prop;
		     prop = icalcomponent_get_next_property(vevent, ICAL_ATTENDEE_PROPERTY)) {

			val = icalproperty_get_attendee(prop);
			if (!val)
				continue;
			if (strncasecmp(val, "mailto:", 7) == 0)
				val += 7;
			if (strcasecmp(val, email) != 0)
				continue;

			/* update or add PARTSTAT parameter */
			param = icalproperty_get_first_parameter(prop, ICAL_PARTSTAT_PARAMETER);
			if (param) {
				icalparameter_set_partstat(param, partstat_to_ical(status));
			} else {
				param = icalparameter_new_partstat(partstat_to_ical(status));
				icalproperty_add_parameter(prop, param);
			}
			found = 1;
		}
	}

	if (!found) {
		icalcomponent_free(root);
		return -1;
	}

	/* write back */
	fp = fopen(ics_path, "w");
	if (!fp) {
		icalcomponent_free(root);
		return -1;
	}

	fprintf(fp, "%s", icalcomponent_as_ical_string(root));
	fclose(fp);
	icalcomponent_free(root);

	return 0;
}

/* helper: remove a property kind from a vevent (all instances) */
static void
remove_props(icalcomponent *vevent, icalproperty_kind kind)
{
	icalproperty *prop, *next;

	for (prop = icalcomponent_get_first_property(vevent, kind);
	     prop; prop = next) {
		next = icalcomponent_get_next_property(vevent, kind);
		icalcomponent_remove_property(vevent, prop);
		icalproperty_free(prop);
	}
}

/* helper: set event times on an existing vevent */
static void
set_event_times(icalcomponent *vevent, const struct event *ev)
{
	struct icaltimetype dtstart, dtend;

	remove_props(vevent, ICAL_DTSTART_PROPERTY);
	remove_props(vevent, ICAL_DTEND_PROPERTY);

	if (ev->all_day) {
		dtstart = icaltime_from_timet_with_zone(ev->start, 1, NULL);
		dtend = icaltime_from_timet_with_zone(ev->end, 1, NULL);
	} else {
		dtstart = icaltime_from_timet_with_zone(ev->start, 0,
		                icaltimezone_get_utc_timezone());
		dtend = icaltime_from_timet_with_zone(ev->end, 0,
		                icaltimezone_get_utc_timezone());
	}
	icalcomponent_add_property(vevent, icalproperty_new_dtstart(dtstart));
	icalcomponent_add_property(vevent, icalproperty_new_dtend(dtend));
}

/* helper: update LAST-MODIFIED and DTSTAMP to now */
static void
update_timestamps(icalcomponent *vevent)
{
	struct icaltimetype now;

	now = icaltime_from_timet_with_zone(time(NULL), 0,
	                icaltimezone_get_utc_timezone());

	remove_props(vevent, ICAL_LASTMODIFIED_PROPERTY);
	icalcomponent_add_property(vevent, icalproperty_new_lastmodified(now));

	remove_props(vevent, ICAL_DTSTAMP_PROPERTY);
	icalcomponent_add_property(vevent, icalproperty_new_dtstamp(now));
}

/* helper: increment SEQUENCE */
static void
bump_sequence(icalcomponent *vevent)
{
	icalproperty *prop;
	int seq = 0;

	prop = icalcomponent_get_first_property(vevent, ICAL_SEQUENCE_PROPERTY);
	if (prop) {
		seq = icalproperty_get_sequence(prop);
		icalproperty_set_sequence(prop, seq + 1);
	} else {
		icalcomponent_add_property(vevent, icalproperty_new_sequence(1));
	}
}

int
ical_add_attendee(const char *ics_path, const char *email, const char *name)
{
	FILE *fp;
	long len;
	char *buf;
	icalcomponent *root, *vevent;
	icalproperty *prop;
	const char *val;
	char mailto[MAX_EMAIL_LEN + 8];

	/* read file */
	fp = fopen(ics_path, "r");
	if (!fp)
		return -1;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len <= 0 || len > 1024 * 1024) {
		fclose(fp);
		return -1;
	}

	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return -1;
	}

	if ((long)fread(buf, 1, len, fp) != len) {
		free(buf);
		fclose(fp);
		return -1;
	}
	buf[len] = '\0';
	fclose(fp);

	root = icalparser_parse_string(buf);
	free(buf);

	if (!root)
		return -1;

	vevent = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
	if (!vevent) {
		icalcomponent_free(root);
		return -1;
	}

	/* check for duplicate */
	for (prop = icalcomponent_get_first_property(vevent, ICAL_ATTENDEE_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property(vevent, ICAL_ATTENDEE_PROPERTY)) {
		val = icalproperty_get_attendee(prop);
		if (val) {
			if (strncasecmp(val, "mailto:", 7) == 0)
				val += 7;
			if (strcasecmp(val, email) == 0) {
				icalcomponent_free(root);
				return -1;
			}
		}
	}

	/* validate email length */
	if (strlen(email) >= MAX_EMAIL_LEN) {
		icalcomponent_free(root);
		return -1;
	}

	/* create ATTENDEE property */
	snprintf(mailto, sizeof(mailto), "mailto:%s", email);
	prop = icalproperty_new_attendee(mailto);
	icalproperty_add_parameter(prop,
		icalparameter_new_partstat(ICAL_PARTSTAT_NEEDSACTION));
	icalproperty_add_parameter(prop,
		icalparameter_new_rsvp(ICAL_RSVP_TRUE));
	if (name && name[0])
		icalproperty_add_parameter(prop, icalparameter_new_cn(name));
	icalcomponent_add_property(vevent, prop);

	update_timestamps(vevent);
	bump_sequence(vevent);

	/* write back */
	fp = fopen(ics_path, "w");
	if (!fp) {
		icalcomponent_free(root);
		return -1;
	}

	fprintf(fp, "%s", icalcomponent_as_ical_string(root));
	fclose(fp);
	icalcomponent_free(root);

	return 0;
}

int
ical_remove_attendee(const char *ics_path, const char *email)
{
	FILE *fp;
	long len;
	char *buf;
	icalcomponent *root, *vevent;
	icalproperty *prop;
	const char *val;
	int found = 0;

	/* read file */
	fp = fopen(ics_path, "r");
	if (!fp)
		return -1;

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len <= 0 || len > 1024 * 1024) {
		fclose(fp);
		return -1;
	}

	buf = malloc(len + 1);
	if (!buf) {
		fclose(fp);
		return -1;
	}

	if ((long)fread(buf, 1, len, fp) != len) {
		free(buf);
		fclose(fp);
		return -1;
	}
	buf[len] = '\0';
	fclose(fp);

	root = icalparser_parse_string(buf);
	free(buf);

	if (!root)
		return -1;

	vevent = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
	if (!vevent) {
		icalcomponent_free(root);
		return -1;
	}

	/* find and remove matching attendee */
	for (prop = icalcomponent_get_first_property(vevent, ICAL_ATTENDEE_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property(vevent, ICAL_ATTENDEE_PROPERTY)) {
		val = icalproperty_get_attendee(prop);
		if (!val)
			continue;
		if (strncasecmp(val, "mailto:", 7) == 0)
			val += 7;
		if (strcasecmp(val, email) == 0) {
			icalcomponent_remove_property(vevent, prop);
			icalproperty_free(prop);
			found = 1;
			break;
		}
	}

	if (!found) {
		icalcomponent_free(root);
		return -1;
	}

	update_timestamps(vevent);
	bump_sequence(vevent);

	/* write back */
	fp = fopen(ics_path, "w");
	if (!fp) {
		icalcomponent_free(root);
		return -1;
	}

	fprintf(fp, "%s", icalcomponent_as_ical_string(root));
	fclose(fp);
	icalcomponent_free(root);

	return 0;
}

/* save event — parse-modify-writeback for existing files, from-scratch for new */
int
ical_save_event(const struct event *ev)
{
	icalcomponent *root, *vevent;
	struct icaltimetype dtstart, dtend;
	FILE *fp;
	int is_existing = 0;

	/* try to parse existing file first */
	if (ev->ics_path[0]) {
		fp = fopen(ev->ics_path, "r");
		if (fp) {
			long len;
			char *buf;

			fseek(fp, 0, SEEK_END);
			len = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			if (len > 0 && len <= 1024 * 1024) {
				buf = malloc(len + 1);
				if (buf) {
					if ((long)fread(buf, 1, len, fp) == len) {
						buf[len] = '\0';
						root = icalparser_parse_string(buf);
						if (root) {
							vevent = icalcomponent_get_first_component(
								root, ICAL_VEVENT_COMPONENT);
							if (vevent)
								is_existing = 1;
							else
								icalcomponent_free(root);
						}
					}
					free(buf);
				}
			}
			fclose(fp);
		}
	}

	if (is_existing) {
		/*
		 * Modify only user-editable properties in-place.
		 * Everything else (attendees, organizer, alarms,
		 * VTIMEZONE, custom X- properties) is preserved.
		 */

		/* summary */
		remove_props(vevent, ICAL_SUMMARY_PROPERTY);
		if (ev->summary[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_summary(ev->summary));

		/* description */
		remove_props(vevent, ICAL_DESCRIPTION_PROPERTY);
		if (ev->description[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_description(ev->description));

		/* location */
		remove_props(vevent, ICAL_LOCATION_PROPERTY);
		if (ev->location[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_location(ev->location));

		/* times */
		set_event_times(vevent, ev);

		/* status */
		remove_props(vevent, ICAL_STATUS_PROPERTY);
		if (ev->status != STATUS_NONE)
			icalcomponent_add_property(vevent,
				icalproperty_new_status(event_status_to_ical(ev->status)));

		/* recurrence */
		remove_props(vevent, ICAL_RRULE_PROPERTY);
		if (ev->recur_freq != RECUR_NONE) {
			struct icalrecurrencetype rrule;
			icalrecurrencetype_clear(&rrule);
			rrule.freq = recur_freq_to_ical(ev->recur_freq);
			rrule.interval = ev->recur_interval > 0 ? ev->recur_interval : 1;
			if (ev->recur_count > 0)
				rrule.count = ev->recur_count;
			if (ev->recur_until > 0)
				rrule.until = icaltime_from_timet_with_zone(
					ev->recur_until, 0, icaltimezone_get_utc_timezone());
			icalcomponent_add_property(vevent, icalproperty_new_rrule(rrule));
		}

		/* update timestamps and sequence */
		update_timestamps(vevent);
		bump_sequence(vevent);
	} else {
		/*
		 * New event — build from scratch.
		 */
		vevent = icalcomponent_new_vevent();

		if (ev->uid[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_uid(ev->uid));
		if (ev->summary[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_summary(ev->summary));
		if (ev->description[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_description(ev->description));
		if (ev->location[0])
			icalcomponent_add_property(vevent,
				icalproperty_new_location(ev->location));

		set_event_times(vevent, ev);

		if (ev->status != STATUS_NONE)
			icalcomponent_add_property(vevent,
				icalproperty_new_status(event_status_to_ical(ev->status)));

		if (ev->recur_freq != RECUR_NONE) {
			struct icalrecurrencetype rrule;
			icalrecurrencetype_clear(&rrule);
			rrule.freq = recur_freq_to_ical(ev->recur_freq);
			rrule.interval = ev->recur_interval > 0 ? ev->recur_interval : 1;
			if (ev->recur_count > 0)
				rrule.count = ev->recur_count;
			if (ev->recur_until > 0)
				rrule.until = icaltime_from_timet_with_zone(
					ev->recur_until, 0, icaltimezone_get_utc_timezone());
			icalcomponent_add_property(vevent, icalproperty_new_rrule(rrule));
		}

		update_timestamps(vevent);

		root = icalcomponent_new_vcalendar();
		icalcomponent_add_property(root, icalproperty_new_version("2.0"));
		icalcomponent_add_property(root,
			icalproperty_new_prodid("-//kc//kai calendar//EN"));
		icalcomponent_add_component(root, vevent);
	}

	/* write to file */
	fp = fopen(ev->ics_path, "w");
	if (!fp) {
		icalcomponent_free(root);
		return -1;
	}

	fprintf(fp, "%s", icalcomponent_as_ical_string(root));
	fclose(fp);
	icalcomponent_free(root);

	return 0;
}

int
ical_delete_event(const struct event *ev)
{
	if (!ev->ics_path[0])
		return -1;
	return remove(ev->ics_path);
}
