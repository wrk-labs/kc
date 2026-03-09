/* See LICENSE file for copyright and license details. */
/* cal.c — date math utilities */

#include <time.h>
#include <string.h>

#include "kc.h"

int
cal_days_in_month(int year, int month)
{
	static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	int m = month - 1;

	if (m < 0 || m > 11)
		return 0;
	if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
		return 29;
	return days[m];
}

/* day of week: 0=Sun, 1=Mon, ... 6=Sat */
int
cal_dow(int year, int month, int day)
{
	struct tm t;

	memset(&t, 0, sizeof(t));
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_isdst = -1;
	mktime(&t);
	return t.tm_wday;
}

int
cal_same_day(const struct tm *a, const struct tm *b)
{
	return a->tm_year == b->tm_year &&
	       a->tm_mon  == b->tm_mon &&
	       a->tm_mday == b->tm_mday;
}

void
cal_today(struct tm *t)
{
	time_t now;

	now = time(NULL);
	localtime_r(&now, t);
}

void
cal_next_day(struct tm *t)
{
	t->tm_mday++;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_prev_day(struct tm *t)
{
	t->tm_mday--;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_next_week(struct tm *t)
{
	t->tm_mday += 7;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_prev_week(struct tm *t)
{
	t->tm_mday -= 7;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_next_month(struct tm *t)
{
	t->tm_mon++;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_prev_month(struct tm *t)
{
	t->tm_mon--;
	t->tm_isdst = -1;
	mktime(t);
}

void
cal_set_today(struct tm *t)
{
	cal_today(t);
}
