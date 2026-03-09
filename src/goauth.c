/* See LICENSE file for copyright and license details. */
/* goauth.c — Google OAuth2 for CalDAV access */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <time.h>
#include <curl/curl.h>

#include "kc.h"
#include "config.h"

#define GOOGLE_AUTH_URL   "https://accounts.google.com/o/oauth2/v2/auth"
#define GOOGLE_TOKEN_URL  "https://oauth2.googleapis.com/token"
#define GOOGLE_SCOPE      "https://www.googleapis.com/auth/calendar"

#define OAUTH_TIMEOUT 120 /* seconds to wait for browser redirect */

/* ---- curl write callback ---- */

struct oauth_buf {
	char  *data;
	size_t len;
	size_t cap;
};

static size_t
oauth_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct oauth_buf *b = userdata;
	size_t total = size * nmemb;

	if (b->len + total > 64 * 1024)
		return 0;

	if (b->len + total + 1 > b->cap) {
		b->cap = (b->len + total + 1) * 2;
		b->data = realloc(b->data, b->cap);
		if (!b->data)
			return 0;
	}
	memcpy(b->data + b->len, ptr, total);
	b->len += total;
	b->data[b->len] = '\0';
	return total;
}

/* ---- simple JSON helpers ---- */

static int
json_get_str(const char *json, const char *key, char *out, size_t outlen)
{
	char pattern[256];
	const char *p, *start, *end;
	size_t len;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;

	p += strlen(pattern);
	while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;

	if (*p != '"')
		return -1;
	start = ++p;

	/* find closing quote, handle escaped quotes */
	end = start;
	while (*end && *end != '"') {
		if (*end == '\\' && end[1])
			end++;
		end++;
	}
	if (*end != '"')
		return -1;

	len = (size_t)(end - start);
	if (len >= outlen)
		len = outlen - 1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

static long
json_get_int(const char *json, const char *key)
{
	char pattern[256];
	const char *p;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;

	p += strlen(pattern);
	while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;

	return atol(p);
}

/* ---- token storage (~/.kc/tokens/<cal_name>) ---- */

static int
tokens_load(const char *home, const char *name,
            char *access, size_t alen,
            char *refresh, size_t rlen,
            time_t *expires_at)
{
	char path[MAX_PATH_LEN];
	FILE *fp;
	char line[4096];

	snprintf(path, sizeof(path), "%s/%s/tokens/%s",
	         home, data_dir, name);
	fp = fopen(path, "r");
	if (!fp)
		return -1;

	access[0] = '\0';
	refresh[0] = '\0';
	*expires_at = 0;

	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		if (strncmp(line, "access_token ", 13) == 0) {
			strncpy(access, line + 13, alen - 1);
			access[alen - 1] = '\0';
		} else if (strncmp(line, "refresh_token ", 14) == 0) {
			strncpy(refresh, line + 14, rlen - 1);
			refresh[rlen - 1] = '\0';
		} else if (strncmp(line, "expires_at ", 11) == 0) {
			*expires_at = (time_t)atol(line + 11);
		}
	}

	memset(line, 0, sizeof(line));
	fclose(fp);
	return (refresh[0] != '\0') ? 0 : -1;
}

static int
tokens_save(const char *home, const char *name,
            const char *access, const char *refresh,
            time_t expires_at)
{
	char path[MAX_PATH_LEN * 2];
	char dir[MAX_PATH_LEN];
	FILE *fp;

	snprintf(dir, sizeof(dir), "%s/%s/tokens", home, data_dir);
	mkdir(dir, 0700);

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return -1;
	fp = fdopen(fd, "w");
	if (!fp) {
		close(fd);
		return -1;
	}

	fprintf(fp, "access_token %s\n", access);
	fprintf(fp, "refresh_token %s\n", refresh);
	fprintf(fp, "expires_at %ld\n", (long)expires_at);
	fclose(fp);
	return 0;
}

/* ---- client credentials (~/.kc/google_oauth) ---- */

int
goauth_load_client(const char *home, char *id, size_t idlen,
                   char *secret, size_t secretlen)
{
	char path[MAX_PATH_LEN];
	FILE *fp;
	char line[1024];

	id[0] = '\0';
	secret[0] = '\0';

	/* try user-provided credentials first */
	snprintf(path, sizeof(path), "%s/%s/google_oauth", home, data_dir);
	fp = fopen(path, "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			line[strcspn(line, "\n")] = '\0';
			if (strncmp(line, "client_id ", 10) == 0) {
				strncpy(id, line + 10, idlen - 1);
				id[idlen - 1] = '\0';
			} else if (strncmp(line, "client_secret ", 14) == 0) {
				strncpy(secret, line + 14, secretlen - 1);
				secret[secretlen - 1] = '\0';
			}
		}
		memset(line, 0, sizeof(line));
		fclose(fp);

		if (id[0] && secret[0])
			return 0;
	}

	/* fall back to compiled-in defaults */
	if (GOOGLE_CLIENT_ID[0] && GOOGLE_CLIENT_SECRET[0]) {
		strncpy(id, GOOGLE_CLIENT_ID, idlen - 1);
		id[idlen - 1] = '\0';
		strncpy(secret, GOOGLE_CLIENT_SECRET, secretlen - 1);
		secret[secretlen - 1] = '\0';
		return 0;
	}

	return -1;
}

int
goauth_save_client(const char *home, const char *id, const char *secret)
{
	char path[MAX_PATH_LEN];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/%s/google_oauth", home, data_dir);
	fp = fopen(path, "w");
	if (!fp)
		return -1;

	fprintf(fp, "client_id %s\n", id);
	fprintf(fp, "client_secret %s\n", secret);
	fclose(fp);
	chmod(path, 0600);
	return 0;
}

/* ---- token refresh ---- */

static int
token_refresh(const char *client_id, const char *client_secret,
              const char *refresh_token,
              char *access_out, size_t alen,
              long *expires_in)
{
	CURL *curl;
	CURLcode res;
	struct oauth_buf resp = {0};
	long http_code;
	char *post;
	size_t post_sz;
	char *esc_id, *esc_secret, *esc_refresh;

	curl = curl_easy_init();
	if (!curl)
		return -1;

	esc_id = curl_easy_escape(curl, client_id, 0);
	esc_secret = curl_easy_escape(curl, client_secret, 0);
	esc_refresh = curl_easy_escape(curl, refresh_token, 0);

	post_sz = strlen(esc_id) + strlen(esc_secret) +
	         strlen(esc_refresh) + 128;
	post = malloc(post_sz);
	if (!post) {
		curl_free(esc_id);
		curl_free(esc_secret);
		curl_free(esc_refresh);
		curl_easy_cleanup(curl);
		return -1;
	}

	snprintf(post, post_sz,
	         "client_id=%s&client_secret=%s&refresh_token=%s"
	         "&grant_type=refresh_token",
	         esc_id, esc_secret, esc_refresh);

	curl_free(esc_id);
	curl_free(esc_secret);
	curl_free(esc_refresh);

	curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_TOKEN_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, oauth_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	memset(post, 0, strlen(post));
	free(post);

	if (res != CURLE_OK || http_code != 200 || !resp.data) {
		free(resp.data);
		return -1;
	}

	if (json_get_str(resp.data, "access_token", access_out, alen) != 0) {
		free(resp.data);
		return -1;
	}

	*expires_in = json_get_int(resp.data, "expires_in");
	if (*expires_in <= 0)
		*expires_in = 3600;

	free(resp.data);
	return 0;
}

/* ---- public API ---- */

/*
 * Get a valid OAuth2 access token for a calendar.
 * Loads stored tokens, refreshes if expired.
 * Returns 0 on success, -1 if tokens missing or refresh fails.
 */
int
goauth_get_token(const char *home, const char *cal_name,
                 char *token, size_t tokenlen)
{
	char access[2048] = {0};
	char refresh[2048] = {0};
	char client_id[512] = {0};
	char client_secret[512] = {0};
	time_t expires_at = 0;

	if (tokens_load(home, cal_name, access, sizeof(access),
	                refresh, sizeof(refresh), &expires_at) != 0)
		return -1;

	/* still valid? (60s safety margin) */
	if (access[0] && time(NULL) < expires_at - 60) {
		strncpy(token, access, tokenlen - 1);
		token[tokenlen - 1] = '\0';
		memset(access, 0, sizeof(access));
		memset(refresh, 0, sizeof(refresh));
		return 0;
	}

	/* need to refresh */
	if (goauth_load_client(home, client_id, sizeof(client_id),
	                       client_secret, sizeof(client_secret)) != 0) {
		memset(access, 0, sizeof(access));
		memset(refresh, 0, sizeof(refresh));
		return -1;
	}

	long expires_in = 0;
	if (token_refresh(client_id, client_secret, refresh,
	                  access, sizeof(access), &expires_in) != 0) {
		memset(access, 0, sizeof(access));
		memset(refresh, 0, sizeof(refresh));
		memset(client_id, 0, sizeof(client_id));
		memset(client_secret, 0, sizeof(client_secret));
		return -1;
	}

	tokens_save(home, cal_name, access, refresh,
	            time(NULL) + expires_in);

	strncpy(token, access, tokenlen - 1);
	token[tokenlen - 1] = '\0';

	memset(access, 0, sizeof(access));
	memset(refresh, 0, sizeof(refresh));
	memset(client_id, 0, sizeof(client_id));
	memset(client_secret, 0, sizeof(client_secret));
	return 0;
}

/*
 * Run the full OAuth2 authorization flow:
 * 1. Bind a localhost socket for the redirect
 * 2. Open browser with Google consent screen
 * 3. Wait for redirect with auth code
 * 4. Exchange code for access + refresh tokens
 * 5. Save tokens to disk
 *
 * Returns 0 on success, -1 on failure.
 */
int
goauth_authorize(const char *home, const char *cal_name,
                 const char *client_id, const char *client_secret)
{
	int srv, cli;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	char redirect_uri[128];
	char auth_url[4096];
	char buf[4096];
	char code[1024] = {0};
	char *p;
	int port, opt = 1;
	ssize_t n;
	fd_set fds;
	struct timeval tv;
	pid_t pid;
	char *enc_redirect, *enc_scope;

	/* create localhost listener */
	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;

	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0; /* OS picks available port */

	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(srv);
		return -1;
	}

	if (getsockname(srv, (struct sockaddr *)&addr, &addrlen) < 0) {
		close(srv);
		return -1;
	}
	port = ntohs(addr.sin_port);

	listen(srv, 1);

	/* build auth URL */
	snprintf(redirect_uri, sizeof(redirect_uri),
	         "http://127.0.0.1:%d", port);

	enc_redirect = curl_easy_escape(NULL, redirect_uri, 0);
	enc_scope = curl_easy_escape(NULL, GOOGLE_SCOPE, 0);

	snprintf(auth_url, sizeof(auth_url),
	         "%s?client_id=%s&redirect_uri=%s&response_type=code"
	         "&scope=%s&access_type=offline&prompt=consent",
	         GOOGLE_AUTH_URL, client_id,
	         enc_redirect ? enc_redirect : redirect_uri,
	         enc_scope ? enc_scope : GOOGLE_SCOPE);

	curl_free(enc_redirect);
	curl_free(enc_scope);

	/* open browser (fork+exec to avoid shell injection) */
	pid = fork();
	if (pid == 0) {
		setsid();
#ifdef __APPLE__
		execlp("open", "open", auth_url, (char *)NULL);
#else
		execlp("xdg-open", "xdg-open", auth_url, (char *)NULL);
#endif
		_exit(1);
	}
	if (pid > 0)
		waitpid(pid, NULL, WNOHANG);

	/* wait for redirect with timeout */
	FD_ZERO(&fds);
	FD_SET(srv, &fds);
	tv.tv_sec = OAUTH_TIMEOUT;
	tv.tv_usec = 0;

	if (select(srv + 1, &fds, NULL, NULL, &tv) <= 0) {
		close(srv);
		return -1;
	}

	cli = accept(srv, NULL, NULL);
	close(srv);

	if (cli < 0)
		return -1;

	/* read HTTP request */
	n = recv(cli, buf, sizeof(buf) - 1, 0);
	if (n <= 0) {
		close(cli);
		return -1;
	}
	buf[n] = '\0';

	/* extract code from: GET /?code=AUTH_CODE&... HTTP/1.1 */
	p = strstr(buf, "code=");
	if (p) {
		char *end;
		size_t codelen;

		p += 5;
		end = p;
		while (*end && *end != '&' && *end != ' ' && *end != '\r')
			end++;
		codelen = (size_t)(end - p);
		if (codelen >= sizeof(code))
			codelen = sizeof(code) - 1;
		memcpy(code, p, codelen);
		code[codelen] = '\0';
	}

	if (!code[0]) {
		/* auth failed or user denied */
		const char *err =
		    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
		    "<html><body><h2>Authorization failed</h2>"
		    "<p>You can close this tab.</p></body></html>";
		send(cli, err, strlen(err), 0);
		close(cli);
		return -1;
	}

	/* send success page */
	{
		const char *ok =
		    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
		    "<html><body><h2>Authorized!</h2>"
		    "<p>You can close this tab and return to kc.</p>"
		    "</body></html>";
		send(cli, ok, strlen(ok), 0);
	}
	close(cli);

	/* exchange auth code for tokens */
	{
		CURL *curl;
		CURLcode res;
		struct oauth_buf resp = {0};
		long http_code;
		char *post;
		size_t post_sz;
		char *esc_code, *esc_id, *esc_secret, *esc_redir;
		char access[2048] = {0};
		char refresh[2048] = {0};
		long expires_in;

		curl = curl_easy_init();
		if (!curl) {
			memset(code, 0, sizeof(code));
			return -1;
		}

		esc_code = curl_easy_escape(curl, code, 0);
		esc_id = curl_easy_escape(curl, client_id, 0);
		esc_secret = curl_easy_escape(curl, client_secret, 0);
		esc_redir = curl_easy_escape(curl, redirect_uri, 0);

		post_sz = strlen(esc_code) + strlen(esc_id) +
		          strlen(esc_secret) + strlen(esc_redir) + 128;
		post = malloc(post_sz);
		if (!post) {
			curl_free(esc_code);
			curl_free(esc_id);
			curl_free(esc_secret);
			curl_free(esc_redir);
			curl_easy_cleanup(curl);
			memset(code, 0, sizeof(code));
			return -1;
		}

		snprintf(post, post_sz,
		         "client_id=%s&client_secret=%s&code=%s"
		         "&redirect_uri=%s&grant_type=authorization_code",
		         esc_id, esc_secret, esc_code, esc_redir);

		curl_free(esc_code);
		curl_free(esc_id);
		curl_free(esc_secret);
		curl_free(esc_redir);

		curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_TOKEN_URL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, oauth_write_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);

		memset(post, 0, strlen(post));
		free(post);
		memset(code, 0, sizeof(code));

		if (res != CURLE_OK || http_code != 200 || !resp.data) {
			free(resp.data);
			return -1;
		}

		json_get_str(resp.data, "access_token", access, sizeof(access));
		json_get_str(resp.data, "refresh_token", refresh, sizeof(refresh));
		expires_in = json_get_int(resp.data, "expires_in");
		if (expires_in <= 0)
			expires_in = 3600;

		free(resp.data);

		if (!access[0] || !refresh[0]) {
			memset(access, 0, sizeof(access));
			memset(refresh, 0, sizeof(refresh));
			return -1;
		}

		tokens_save(home, cal_name, access, refresh,
		            time(NULL) + expires_in);

		memset(access, 0, sizeof(access));
		memset(refresh, 0, sizeof(refresh));
	}

	return 0;
}

int
goauth_remove_tokens(const char *home, const char *cal_name)
{
	char path[MAX_PATH_LEN];

	snprintf(path, sizeof(path), "%s/%s/tokens/%s",
	         home, data_dir, cal_name);
	unlink(path);
	return 0;
}
