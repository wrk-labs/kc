/* See LICENSE file for copyright and license details. */
/* caldav.c — native CalDAV sync using libcurl */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <curl/curl.h>

#include "kc.h"
#include "config.h"

/* ---- memory buffer for curl responses ---- */

struct buf {
	char *data;
	size_t len;
	size_t cap;
};

static void
buf_init(struct buf *b)
{
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

static void
buf_free(struct buf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

#define MAX_RESPONSE_SIZE (16 * 1024 * 1024)  /* 16MB hard limit */

static size_t
write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct buf *b = userdata;
	size_t total = size * nmemb;

	/* reject excessively large responses */
	if (b->len + total > MAX_RESPONSE_SIZE)
		return 0;

	if (b->len + total + 1 > b->cap) {
		size_t needed = b->len + total + 1;
		b->cap = needed < MAX_RESPONSE_SIZE / 2 ? needed * 2 : MAX_RESPONSE_SIZE + 1;
		b->data = realloc(b->data, b->cap);
		if (!b->data)
			return 0;
	}
	memcpy(b->data + b->len, ptr, total);
	b->len += total;
	b->data[b->len] = '\0';
	return total;
}

/* ---- XML helpers ---- */

/*
 * Find content between <[ns:]tag> and </[ns:]tag> starting from 'from'.
 * Handles arbitrary namespace prefixes (d:href, D:href, DAV:href, href).
 * Returns pointer to content start, sets *len to content length.
 * Returns NULL if not found.
 */
static const char *
xml_find(const char *xml, const char *tag, const char *from, int *len)
{
	const char *p, *end;
	char close_tag[256];

	if (!from)
		from = xml;

	p = from;
	while (*p) {
		if (*p != '<') { p++; continue; }

		/* check for <tag> or <ns:tag> */
		const char *tstart = p + 1;
		const char *colon = NULL;
		const char *tname = tstart;

		/* skip namespace prefix — limit scan to prevent runaway */
		int scan = 0;
		while (*tname && *tname != '>' && *tname != ' ' && *tname != '/' && scan < 200) {
			if (*tname == ':') {
				colon = tname;
			}
			tname++;
			scan++;
		}

		/* the tag name is after the last colon (if any) */
		const char *actual_tag;
		int tag_end;
		if (colon) {
			actual_tag = colon + 1;
			tag_end = (int)(tname - actual_tag);
		} else {
			actual_tag = tstart;
			tag_end = (int)(tname - actual_tag);
		}

		/* skip overly long namespace/tag combinations */
		if (tag_end > 100 || (colon && (int)(colon - tstart) > 100)) {
			p++;
			continue;
		}

		if (tag_end == (int)strlen(tag) &&
		    strncmp(actual_tag, tag, tag_end) == 0 &&
		    (*tname == '>' || *tname == ' ')) {
			/* found opening tag — find close */
			const char *content = strchr(tname, '>');
			if (!content) { p++; continue; }
			content++;

			/* build close tag pattern — try with and without ns prefix */
			if (colon) {
				int nslen = (int)(colon - tstart);
				snprintf(close_tag, sizeof(close_tag),
				         "</%.*s:%s>", nslen, tstart, tag);
			} else {
				snprintf(close_tag, sizeof(close_tag),
				         "</%s>", tag);
			}
			end = strstr(content, close_tag);
			if (!end) {
				/* try without namespace */
				snprintf(close_tag, sizeof(close_tag),
				         "</%s>", tag);
				end = strstr(content, close_tag);
			}
			if (end) {
				*len = (int)(end - content);
				return content;
			}
		}
		p++;
	}
	return NULL;
}

/*
 * Extract text content of an XML tag into a buffer.
 * Returns 0 on success, -1 if not found.
 */
static int
xml_get(const char *xml, const char *tag, const char *from,
        char *out, size_t outlen)
{
	int len;
	const char *content = xml_find(xml, tag, from, &len);

	if (!content)
		return -1;

	/* trim whitespace */
	while (len > 0 && (*content == ' ' || *content == '\n' ||
	       *content == '\r' || *content == '\t')) {
		content++;
		len--;
	}
	while (len > 0 && (content[len-1] == ' ' || content[len-1] == '\n' ||
	       content[len-1] == '\r' || content[len-1] == '\t')) {
		len--;
	}

	/* might contain nested tags like <href>...</href> — extract recursively */
	if (len > 0 && content[0] == '<') {
		/* look for an href inside */
		int inner_len;
		const char *inner = xml_find(content, "href", content, &inner_len);
		if (inner) {
			content = inner;
			len = inner_len;
			/* trim again */
			while (len > 0 && (*content == ' ' || *content == '\n')) {
				content++; len--;
			}
			while (len > 0 && (content[len-1] == ' ' || content[len-1] == '\n')) {
				len--;
			}
		}
	}

	if (len <= 0)
		return -1;
	if ((size_t)len >= outlen)
		len = (int)outlen - 1;
	memcpy(out, content, len);
	out[len] = '\0';
	return 0;
}

/* ---- URL helpers ---- */

static void
resolve_url(const char *base, const char *href, char *out, size_t outlen)
{
	if (!href || !href[0]) {
		strncpy(out, base, outlen - 1);
		out[outlen - 1] = '\0';
		return;
	}

	if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) {
		strncpy(out, href, outlen - 1);
		out[outlen - 1] = '\0';
		return;
	}

	if (href[0] == '/') {
		/* absolute path — prepend scheme://host from base */
		const char *p = strstr(base, "://");
		if (p) {
			p += 3;
			const char *slash = strchr(p, '/');
			if (slash)
				snprintf(out, outlen, "%.*s%s",
				         (int)(slash - base), base, href);
			else
				snprintf(out, outlen, "%s%s", base, href);
		} else {
			strncpy(out, href, outlen - 1);
			out[outlen - 1] = '\0';
		}
		return;
	}

	/* relative — append to base */
	size_t blen = strlen(base);
	if (blen > 0 && base[blen - 1] == '/')
		snprintf(out, outlen, "%s%s", base, href);
	else
		snprintf(out, outlen, "%s/%s", base, href);
}

/*
 * Check if a host string is an IP address literal (IPv4 or IPv6).
 * IPv6 literals in URLs are enclosed in brackets: [::1]
 * Returns 1 if IP literal, 0 if hostname.
 */
static int
is_ip_literal(const char *host, int hostlen)
{
	int i, dots = 0, all_digits = 1;

	/* IPv6 bracket notation */
	if (hostlen > 0 && host[0] == '[')
		return 1;

	/* check for IPv4: all digits and dots, 1-3 dots */
	for (i = 0; i < hostlen; i++) {
		if (host[i] == '.')
			dots++;
		else if (host[i] < '0' || host[i] > '9')
			all_digits = 0;
	}
	if (all_digits && dots >= 1 && dots <= 3)
		return 1;

	return 0;
}

/*
 * Extract the registrable domain from a host (last two segments).
 * e.g. "p164-caldav.icloud.com" → "icloud.com"
 *      "caldav.icloud.com" → "icloud.com"
 *      "example.com" → "example.com"
 * Returns NULL for IP addresses (no domain comparison possible).
 */
static const char *
base_domain(const char *host, int hostlen)
{
	const char *p;
	int dots = 0;

	/* reject IP literals — must use exact match */
	if (is_ip_literal(host, hostlen))
		return NULL;

	/* strip port */
	p = memchr(host, ':', hostlen);
	if (p)
		hostlen = (int)(p - host);

	p = host + hostlen;
	while (p > host) {
		p--;
		if (*p == '.') {
			dots++;
			if (dots == 2)
				return p + 1;
		}
	}
	return host;
}

/*
 * Validate that a resolved URL shares the same registrable domain
 * as the base URL. Allows subdomain variation (e.g. caldav.icloud.com
 * vs p164-caldav.icloud.com). Prevents SSRF to unrelated hosts.
 */
static int
same_host(const char *base_url, const char *url)
{
	const char *bp, *up, *bs, *us;
	const char *bd, *ud;
	int bl, ul;

	bp = strstr(base_url, "://");
	up = strstr(url, "://");
	if (!bp || !up)
		return 0;
	bp += 3;
	up += 3;
	bs = strchr(bp, '/');
	us = strchr(up, '/');
	if (!bs) bs = bp + strlen(bp);
	if (!us) us = up + strlen(up);

	bl = (int)(bs - bp);
	ul = (int)(us - up);

	/* strip port from comparison */
	{
		const char *cp = memchr(bp, ':', bl);
		if (cp) bl = (int)(cp - bp);
		cp = memchr(up, ':', ul);
		if (cp) ul = (int)(cp - up);
	}

	/* exact match */
	if (bl == ul && strncasecmp(bp, up, bl) == 0)
		return 1;

	/* compare base domains (e.g. icloud.com) */
	bd = base_domain(bp, bl);
	ud = base_domain(up, ul);

	/* IP literals: base_domain returns NULL, require exact host match */
	if (!bd || !ud)
		return 0;

	{
		int bdl = (int)strlen(bd);
		int udl = (int)strlen(ud);
		/* strip port from base domain strings */
		const char *cp = memchr(bd, ':', bdl);
		if (cp) bdl = (int)(cp - bd);
		cp = memchr(ud, ':', udl);
		if (cp) udl = (int)(cp - ud);

		if (bdl == udl && bdl > 0 && strncasecmp(bd, ud, bdl) == 0)
			return 1;
	}

	return 0;
}

/* extract filename from an href path */
static const char *
href_filename(const char *href)
{
	const char *p = strrchr(href, '/');
	return p ? p + 1 : href;
}

/* ---- logging ---- */

static void
sync_log(const char *fmt, ...)
{
	char path[MAX_PATH_LEN];
	const char *home = getenv("HOME");
	FILE *fp;
	va_list ap;

	if (!home) return;

	snprintf(path, sizeof(path), "%s/%s/sync.log", home, data_dir);
	fp = fopen(path, "a");
	if (!fp) return;

	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fprintf(fp, "\n");
	fclose(fp);
}

/* ---- curl helpers ---- */

static CURL *
make_curl(const char *url, const char *user, const char *pass,
          struct buf *response)
{
	CURL *curl = curl_easy_init();

	if (!curl)
		return NULL;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "kc/" VERSION);
#ifdef CURLOPT_PROTOCOLS_STR
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

	if (user && user[0] && pass && pass[0]) {
		curl_easy_setopt(curl, CURLOPT_USERNAME, user);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	}

	return curl;
}

static int
do_propfind(const char *url, const char *user, const char *pass,
            int depth, const char *body, struct buf *response)
{
	CURL *curl;
	struct curl_slist *headers = NULL;
	char depth_hdr[32];
	long http_code;
	CURLcode res;

	buf_init(response);

	curl = make_curl(url, user, pass, response);
	if (!curl)
		return -1;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
	snprintf(depth_hdr, sizeof(depth_hdr), "Depth: %d", depth);
	headers = curl_slist_append(headers, depth_hdr);
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	if (body) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
	}

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || (http_code != 207 && http_code != 200)) {
		sync_log("PROPFIND %s → HTTP %ld, curl %d (%s)",
		         url, http_code, (int)res, curl_easy_strerror(res));
		return -1;
	}

	return 0;
}

static int
do_get(const char *url, const char *user, const char *pass,
       struct buf *response)
{
	CURL *curl;
	long http_code;
	CURLcode res;

	buf_init(response);

	curl = make_curl(url, user, pass, response);
	if (!curl)
		return -1;

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK || http_code != 200)
		return -1;

	return 0;
}

static int
do_put(const char *url, const char *user, const char *pass,
       const char *data, size_t datalen, const char *etag)
{
	CURL *curl;
	struct curl_slist *headers = NULL;
	struct buf response;
	long http_code;
	CURLcode res;
	char if_match[512];

	buf_init(&response);

	curl = make_curl(url, user, pass, &response);
	if (!curl)
		return -1;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	headers = curl_slist_append(headers, "Content-Type: text/calendar; charset=utf-8");

	if (etag && etag[0]) {
		snprintf(if_match, sizeof(if_match), "If-Match: %s", etag);
		headers = curl_slist_append(headers, if_match);
	}
	/* no etag = unconditional PUT (works for both create and update) */

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)datalen);

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	buf_free(&response);

	if (res != CURLE_OK)
		return -1;
	/* 200 OK, 201 Created, or 204 No Content */
	if (http_code != 200 && http_code != 201 && http_code != 204)
		return -1;

	return 0;
}

static int
do_delete(const char *url, const char *user, const char *pass)
{
	CURL *curl;
	struct buf response;
	long http_code;
	CURLcode res;

	buf_init(&response);

	curl = make_curl(url, user, pass, &response);
	if (!curl)
		return -1;

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);
	buf_free(&response);

	if (res != CURLE_OK)
		return -1;
	/* 204 No Content or 200 OK */
	if (http_code != 204 && http_code != 200)
		return -1;

	return 0;
}

/* ---- filename safety ---- */

/*
 * Validate a filename from a CalDAV href to prevent path traversal
 * and other attacks. Only allows [a-zA-Z0-9._-] characters.
 * Returns 1 if safe, 0 if not.
 */
static int
safe_filename(const char *name)
{
	const char *p;

	if (!name || !*name)
		return 0;

	/* reject leading dot (hidden files, ".." traversal) */
	if (name[0] == '.')
		return 0;

	/* reject if contains path separator */
	if (strchr(name, '/') || strchr(name, '\\'))
		return 0;

	/* only allow safe characters */
	for (p = name; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') ||
		    (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') ||
		    *p == '.' || *p == '-' || *p == '_' ||
		    *p == '@')
			continue;
		return 0;
	}

	/* must end with .ics */
	size_t len = strlen(name);
	if (len < 5 || strcmp(name + len - 4, ".ics") != 0)
		return 0;

	/* reject overly long names */
	if (len > 255)
		return 0;

	return 1;
}

/* ---- sync state ---- */

#define MAX_SYNC_ENTRIES 2048

struct sync_entry {
	char filename[256];
	char etag[256];
	char href[MAX_PATH_LEN];
};

struct sync_state {
	struct sync_entry entries[MAX_SYNC_ENTRIES];
	int n;
};

static void
state_load(const char *cal_path, struct sync_state *ss)
{
	char path[MAX_PATH_LEN + 16];
	FILE *fp;
	char line[2048];

	memset(ss, 0, sizeof(*ss));

	snprintf(path, sizeof(path), "%s/.caldav_state", cal_path);
	fp = fopen(path, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp) && ss->n < MAX_SYNC_ENTRIES) {
		struct sync_entry *e = &ss->entries[ss->n];
		char *tab1, *tab2;

		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;

		/* format: filename\tetag\thref */
		tab1 = strchr(line, '\t');
		if (!tab1) continue;
		*tab1 = '\0';
		tab2 = strchr(tab1 + 1, '\t');
		if (!tab2) continue;
		*tab2 = '\0';

		strncpy(e->filename, line, sizeof(e->filename) - 1);
		strncpy(e->etag, tab1 + 1, sizeof(e->etag) - 1);
		strncpy(e->href, tab2 + 1, sizeof(e->href) - 1);
		ss->n++;
	}

	fclose(fp);
}

static void
state_save(const char *cal_path, const struct sync_state *ss)
{
	char path[MAX_PATH_LEN + 16];
	FILE *fp;
	int i;

	snprintf(path, sizeof(path), "%s/.caldav_state", cal_path);
	fp = fopen(path, "w");
	if (!fp)
		return;

	fprintf(fp, "# caldav sync state — do not edit\n");
	for (i = 0; i < ss->n; i++) {
		fprintf(fp, "%s\t%s\t%s\n",
		        ss->entries[i].filename,
		        ss->entries[i].etag,
		        ss->entries[i].href);
	}

	fclose(fp);
}

static struct sync_entry *
state_find_by_href(struct sync_state *ss, const char *href)
{
	int i;
	for (i = 0; i < ss->n; i++) {
		if (strcmp(ss->entries[i].href, href) == 0)
			return &ss->entries[i];
	}
	return NULL;
}

static struct sync_entry *
state_find_by_file(struct sync_state *ss, const char *filename)
{
	int i;
	for (i = 0; i < ss->n; i++) {
		if (strcmp(ss->entries[i].filename, filename) == 0)
			return &ss->entries[i];
	}
	return NULL;
}

/* ---- password reading ---- */

int
read_secret(const char *home, const char *name, char *pass, size_t passlen)
{
	char spath[MAX_PATH_LEN];
	FILE *fp;
	char line[1024];
	int prefix_len = (int)strlen(name);

	snprintf(spath, sizeof(spath), "%s/%s/secrets",
	         home, data_dir);

	fp = fopen(spath, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		if (strncmp(line, name, prefix_len) == 0 && line[prefix_len] == ' ') {
			strncpy(pass, line + prefix_len + 1, passlen - 1);
			pass[passlen - 1] = '\0';
			memset(line, 0, sizeof(line));
			fclose(fp);
			return 0;
		}
	}

	memset(line, 0, sizeof(line));
	fclose(fp);
	return -1;
}

/* ---- discovery ---- */

/*
 * CalDAV discovery chain:
 * 1. PROPFIND base URL → current-user-principal
 * 2. PROPFIND principal → calendar-home-set
 * 3. PROPFIND home-set depth 1 → list of calendar collections
 *
 * For Google, the configured URL already points to a calendar,
 * so we can skip discovery and use it directly.
 */

static const char *propfind_principal =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<d:propfind xmlns:d=\"DAV:\">"
	"<d:prop><d:current-user-principal/></d:prop>"
	"</d:propfind>";

static const char *propfind_homeset =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
	"<d:prop><c:calendar-home-set/></d:prop>"
	"</d:propfind>";

static const char *propfind_calendars =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
	"xmlns:cs=\"http://calendarserver.org/ns/\">"
	"<d:prop>"
	"<d:resourcetype/>"
	"<d:displayname/>"
	"<cs:getctag/>"
	"</d:prop>"
	"</d:propfind>";

static const char *propfind_etags =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<d:propfind xmlns:d=\"DAV:\">"
	"<d:prop>"
	"<d:getetag/>"
	"<d:getcontenttype/>"
	"</d:prop>"
	"</d:propfind>";

int
caldav_discover(const char *base_url, const char *user, const char *pass,
                char (*names)[MAX_NAME_LEN], char (*urls)[MAX_PATH_LEN],
                int max_results)
{
	struct buf resp;
	char principal_href[MAX_PATH_LEN] = {0};
	char principal_url[MAX_PATH_LEN];
	char homeset_href[MAX_PATH_LEN] = {0};
	char homeset_url[MAX_PATH_LEN];
	int count = 0;

	/* step 1: get current-user-principal */
	if (do_propfind(base_url, user, pass, 0, propfind_principal, &resp) != 0) {
		sync_log("discover: PROPFIND principal failed for %s", base_url);
		return -1;
	}

	xml_get(resp.data, "current-user-principal", NULL,
	        principal_href, sizeof(principal_href));
	buf_free(&resp);

	if (!principal_href[0]) {
		sync_log("discover: no current-user-principal found");
		return -1;
	}

	resolve_url(base_url, principal_href, principal_url, sizeof(principal_url));
	sync_log("discover: principal = %s", principal_url);

	/* step 2: get calendar-home-set */
	if (do_propfind(principal_url, user, pass, 0, propfind_homeset, &resp) != 0) {
		sync_log("discover: PROPFIND homeset failed for %s", principal_url);
		return -1;
	}

	xml_get(resp.data, "calendar-home-set", NULL,
	        homeset_href, sizeof(homeset_href));
	buf_free(&resp);

	if (!homeset_href[0]) {
		sync_log("discover: no calendar-home-set found");
		return -1;
	}

	resolve_url(base_url, homeset_href, homeset_url, sizeof(homeset_url));
	sync_log("discover: homeset = %s", homeset_url);

	/* step 3: list calendar collections */
	if (do_propfind(homeset_url, user, pass, 1, propfind_calendars, &resp) != 0) {
		sync_log("discover: PROPFIND calendars failed for %s", homeset_url);
		return -1;
	}

	{
		/* parse multistatus — find each <response> */
		const char *p = resp.data;
		while (p && count < max_results) {
			int resp_len;
			const char *response_block = xml_find(p, "response", p, &resp_len);
			if (!response_block)
				break;

			/* check if this is a calendar (has <calendar/> in resourcetype) */
			char href[MAX_PATH_LEN] = {0};
			char name[MAX_NAME_LEN] = {0};

			xml_get(response_block, "href", response_block,
			        href, sizeof(href));

			/* skip the home-set URL itself (it's a collection, not a calendar) */
			if (href[0] && strcmp(href, homeset_href) != 0) {
				/* check for calendar resourcetype —
			 * look for <calendar or :calendar in resourcetype,
			 * but not "calendarserver" (namespace URI) */
				int rt_len;
				const char *rt = xml_find(response_block, "resourcetype",
				                          response_block, &rt_len);
				int is_calendar = 0;
				if (rt) {
					const char *s = rt;
					const char *rt_end = rt + rt_len;
					while (s < rt_end && !is_calendar) {
						s = strstr(s, "calendar");
						if (!s || s >= rt_end) break;
						/* reject "calendarserver" */
						if (s + 8 < rt_end &&
						    (s[8] == '/' || s[8] == '>' ||
						     s[8] == ' ' || s[8] == '"'))
							is_calendar = 1;
						else
							s += 8;
					}
				}
				if (is_calendar) {
					xml_get(response_block, "displayname",
					        response_block, name, sizeof(name));

					if (!name[0]) {
						/* use last path segment as name */
						const char *fn = href_filename(href);
						if (fn && *fn) {
							strncpy(name, fn, MAX_NAME_LEN - 1);
							/* remove trailing slash */
							size_t nl = strlen(name);
							if (nl > 0 && name[nl-1] == '/')
								name[nl-1] = '\0';
						}
					}

					if (name[0] && href[0]) {
						strncpy(names[count], name, MAX_NAME_LEN - 1);
						resolve_url(homeset_url, href,
						            urls[count], MAX_PATH_LEN);
						count++;
					}
				}
			}

			p = response_block + resp_len;
		}
	}

	buf_free(&resp);
	return count;
}

/* ---- single event upload ---- */

/*
 * Upload a single .ics file to the CalDAV server.
 * Reads the local file, builds the remote URL from the calendar's
 * CalDAV URL + filename, and PUTs it.
 * Returns 0 on success, -1 on failure.
 */
int
caldav_put_event(const struct calendar *cal, const char *ics_path)
{
	const char *home = getenv("HOME");
	char pass[MAX_NAME_LEN] = {0};
	char upload_url[MAX_PATH_LEN * 2];
	const char *fname;
	FILE *fp;
	char *data;
	long fsize;
	size_t nread, ulen;
	int ret;

	if (!home || !cal->caldav || !cal->caldav_url[0])
		return -1;
	if (!ics_path || !ics_path[0])
		return -1;

	/* extract filename from path */
	fname = strrchr(ics_path, '/');
	fname = fname ? fname + 1 : ics_path;
	if (!fname[0])
		return -1;

	if (read_secret(home, cal->name, pass, sizeof(pass)) != 0)
		return -1;

	/* read local file */
	fp = fopen(ics_path, "r");
	if (!fp) {
		memset(pass, 0, sizeof(pass));
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsize <= 0 || fsize > 1024 * 1024) {
		fclose(fp);
		memset(pass, 0, sizeof(pass));
		return -1;
	}
	data = malloc(fsize + 1);
	if (!data) {
		fclose(fp);
		memset(pass, 0, sizeof(pass));
		return -1;
	}
	nread = fread(data, 1, fsize, fp);
	data[nread] = '\0';
	fclose(fp);

	/* build upload URL */
	ulen = strlen(cal->caldav_url);
	if (ulen > 0 && cal->caldav_url[ulen - 1] == '/')
		snprintf(upload_url, sizeof(upload_url),
		         "%s%s", cal->caldav_url, fname);
	else
		snprintf(upload_url, sizeof(upload_url),
		         "%s/%s", cal->caldav_url, fname);

	ret = do_put(upload_url, cal->caldav_user, pass, data, nread, NULL);

	free(data);
	memset(pass, 0, sizeof(pass));
	return ret;
}

/*
 * Delete an event from the CalDAV server.
 * Builds the remote URL from calendar URL + filename and sends DELETE.
 */
int
caldav_delete_event(const struct calendar *cal, const char *ics_path)
{
	const char *home = getenv("HOME");
	char pass[MAX_NAME_LEN] = {0};
	char delete_url[MAX_PATH_LEN * 2];
	const char *fname;
	size_t ulen;
	int ret;

	if (!home || !cal->caldav || !cal->caldav_url[0])
		return -1;
	if (!ics_path || !ics_path[0])
		return -1;

	fname = strrchr(ics_path, '/');
	fname = fname ? fname + 1 : ics_path;
	if (!fname[0])
		return -1;

	if (read_secret(home, cal->name, pass, sizeof(pass)) != 0)
		return -1;

	ulen = strlen(cal->caldav_url);
	if (ulen > 0 && cal->caldav_url[ulen - 1] == '/')
		snprintf(delete_url, sizeof(delete_url),
		         "%s%s", cal->caldav_url, fname);
	else
		snprintf(delete_url, sizeof(delete_url),
		         "%s/%s", cal->caldav_url, fname);

	ret = do_delete(delete_url, cal->caldav_user, pass);
	memset(pass, 0, sizeof(pass));
	return ret;
}

/* ---- sync calendar (download) ---- */

/*
 * Sync a CalDAV calendar:
 * 1. PROPFIND depth 1 to get all event hrefs + etags
 * 2. Compare with stored sync state
 * 3. Download new/changed events
 * 4. Delete locally removed events
 * 5. Upload new local events
 */
int
caldav_sync_calendar(const struct calendar *cal)
{
	struct buf resp;
	struct sync_state *old_state, *new_state;
	char pass[MAX_NAME_LEN] = {0};
	const char *home = getenv("HOME");
	int downloads = 0, deletes = 0, uploads = 0;

	if (!home || !cal->caldav || !cal->caldav_url[0])
		return -1;

	/* read password */
	if (read_secret(home, cal->name, pass, sizeof(pass)) != 0) {
		sync_log("error: no password for calendar '%s'", cal->name);
		return -1;
	}

	/* allocate sync state on heap (too large for stack) */
	old_state = calloc(1, sizeof(*old_state));
	new_state = calloc(1, sizeof(*new_state));
	if (!old_state || !new_state) {
		free(old_state);
		free(new_state);
		return -1;
	}

	/* load previous sync state */
	state_load(cal->path, old_state);

	/* PROPFIND depth 1 to list all events + etags */
	if (do_propfind(cal->caldav_url, cal->caldav_user, pass,
	                1, propfind_etags, &resp) != 0) {
		sync_log("error: PROPFIND failed for '%s'", cal->name);
		free(old_state);
		free(new_state);
		memset(pass, 0, sizeof(pass));
		return -1;
	}

	/* parse response — extract hrefs and etags */
	{
		const char *p = resp.data;
		while (p) {
			int resp_len;
			const char *response_block = xml_find(p, "response", p, &resp_len);
			if (!response_block)
				break;

			char href[MAX_PATH_LEN] = {0};
			char etag[256] = {0};
			char content_type[256] = {0};

			xml_get(response_block, "href", response_block,
			        href, sizeof(href));
			xml_get(response_block, "getetag", response_block,
			        etag, sizeof(etag));
			xml_get(response_block, "getcontenttype", response_block,
			        content_type, sizeof(content_type));

			p = response_block + resp_len;

			/* skip non-.ics entries (directories, etc.) */
			if (!href[0] || !etag[0])
				continue;

			size_t hlen = strlen(href);
			if (hlen < 5)
				continue;

			/* check if it's an ics file (by extension or content type) */
			int is_ics = 0;
			if (strcmp(href + hlen - 4, ".ics") == 0)
				is_ics = 1;
			if (content_type[0] && strstr(content_type, "text/calendar"))
				is_ics = 1;
			if (!is_ics)
				continue;

			const char *fname = href_filename(href);
			if (!fname || !*fname)
				continue;

			/* sanitize filename — prevent path traversal */
			if (!safe_filename(fname))
				continue;

			/* check if etag changed from old state */
			struct sync_entry *old = state_find_by_href(old_state, href);

			if (!old || strcmp(old->etag, etag) != 0) {
				/* new or changed — download */
				struct buf ics_resp;
				char full_url[MAX_PATH_LEN * 2];
				char local_path[MAX_PATH_LEN * 2];

				resolve_url(cal->caldav_url, href,
				            full_url, sizeof(full_url));

				/* prevent SSRF — only fetch from same host */
				if (!same_host(cal->caldav_url, full_url)) {
					continue;
				}

				if (do_get(full_url, cal->caldav_user, pass, &ics_resp) == 0
				    && ics_resp.data && ics_resp.len > 0
				    && ics_resp.len < 1024 * 1024) { /* 1MB limit */
					FILE *fp;
					snprintf(local_path, sizeof(local_path),
					         "%s/%s", cal->path, fname);
					fp = fopen(local_path, "w");
					if (fp) {
						fwrite(ics_resp.data, 1, ics_resp.len, fp);
						fclose(fp);
						downloads++;
					}
				}
				buf_free(&ics_resp);
			}

			/* add to new state */
			if (new_state->n < MAX_SYNC_ENTRIES) {
				struct sync_entry *e = &new_state->entries[new_state->n];
				strncpy(e->filename, fname, sizeof(e->filename) - 1);
				strncpy(e->etag, etag, sizeof(e->etag) - 1);
				strncpy(e->href, href, sizeof(e->href) - 1);
				new_state->n++;
			}
		}
	}
	buf_free(&resp);

	/* delete local files that no longer exist on server */
	{
		int i;
		for (i = 0; i < old_state->n; i++) {
			if (!state_find_by_href(new_state, old_state->entries[i].href)) {
				char local_path[MAX_PATH_LEN * 2];
				snprintf(local_path, sizeof(local_path),
				         "%s/%s", cal->path, old_state->entries[i].filename);
				unlink(local_path);
				deletes++;
			}
		}
	}

	/* upload new local files (not tracked in state) */
	{
		DIR *dir = opendir(cal->path);
		struct dirent *ent;
		if (dir) {
			while ((ent = readdir(dir)) != NULL) {
				size_t len = strlen(ent->d_name);
				if (len < 5 || strcmp(ent->d_name + len - 4, ".ics") != 0)
					continue;

				/* skip if already tracked (exists on server) */
				if (state_find_by_file(new_state, ent->d_name))
					continue;

				/* skip if it was in old state (means server deleted it) */
				if (state_find_by_file(old_state, ent->d_name))
					continue;

				/* new local file — upload */
				char local_path[MAX_PATH_LEN * 2];
				char upload_url[MAX_PATH_LEN * 2];
				FILE *fp;

				snprintf(local_path, sizeof(local_path),
				         "%s/%s", cal->path, ent->d_name);

				fp = fopen(local_path, "r");
				if (fp) {
					fseek(fp, 0, SEEK_END);
					long fsize = ftell(fp);
					fseek(fp, 0, SEEK_SET);

					if (fsize > 0 && fsize < 1024 * 1024) {
						char *data = malloc(fsize + 1);
						if (data) {
							size_t nread = fread(data, 1, fsize, fp);
							data[nread] = '\0';

							/* build upload URL */
							size_t ulen = strlen(cal->caldav_url);
							if (ulen > 0 && cal->caldav_url[ulen-1] == '/')
								snprintf(upload_url, sizeof(upload_url),
								         "%s%s", cal->caldav_url, ent->d_name);
							else
								snprintf(upload_url, sizeof(upload_url),
								         "%s/%s", cal->caldav_url, ent->d_name);

							if (do_put(upload_url, cal->caldav_user, pass,
							           data, nread, NULL) == 0) {
								uploads++;
							}
							free(data);
						}
					}
					fclose(fp);
				}
			}
			closedir(dir);
		}
	}

	/* save new sync state */
	state_save(cal->path, new_state);

	sync_log("synced '%s': %d downloaded, %d deleted, %d uploaded",
	         cal->name, downloads, deletes, uploads);

	/* clear sensitive data from memory */
	memset(pass, 0, sizeof(pass));

	free(old_state);
	free(new_state);
	return 0;
}
