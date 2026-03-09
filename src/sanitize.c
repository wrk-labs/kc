/* See LICENSE file for copyright and license details. */
/* sanitize.c — input sanitization for untrusted data */

#include <string.h>
#include <ctype.h>

#include "kc.h"

/*
 * Strip dangerous control characters from a string in-place.
 * Keeps: printable chars, newline (\n), tab (\t), and valid UTF-8 sequences.
 * Removes: NUL-adjacent controls, escape (\x1b), BEL, BS, DEL, etc.
 *
 * This is applied to all text loaded from .ics files (summary,
 * description, location, attendee names/emails, organizer fields)
 * to prevent terminal escape injection and control character abuse.
 */
void
sanitize_str(char *s)
{
	char *r = s, *w = s;

	while (*r) {
		unsigned char c = (unsigned char)*r;

		if (c == '\n' || c == '\t') {
			/* keep newlines and tabs */
			*w++ = *r++;
		} else if (c < 0x20 || c == 0x7f) {
			/* strip control characters (including ESC \x1b) */
			r++;
		} else if (c >= 0x80) {
			/* pass through UTF-8 sequences (validate lead byte) */
			int seqlen = 0;
			if ((c & 0xe0) == 0xc0) seqlen = 2;
			else if ((c & 0xf0) == 0xe0) seqlen = 3;
			else if ((c & 0xf8) == 0xf0) seqlen = 4;
			else { r++; continue; } /* invalid lead byte — skip */

			/* check continuation bytes */
			int valid = 1, i;
			for (i = 1; i < seqlen; i++) {
				if (((unsigned char)r[i] & 0xc0) != 0x80) {
					valid = 0;
					break;
				}
			}
			if (valid) {
				for (i = 0; i < seqlen; i++)
					*w++ = *r++;
			} else {
				r++; /* skip invalid lead byte */
			}
		} else {
			/* printable ASCII */
			*w++ = *r++;
		}
	}
	*w = '\0';
}

/*
 * Sanitize a name used in file paths, shell commands, and config files.
 * Only allows: a-z, A-Z, 0-9, dash, underscore, dot (not leading).
 * Replaces everything else with underscore. Enforces max length.
 * Prevents path traversal (no ".." sequences, no leading dot).
 *
 * Used for: calendar names, which appear in:
 *   - file paths:  ~/.kc/calendars/<name>/
 *   - secrets:     <name> <password>
 *   - config:      calendar <name>
 */
void
sanitize_name(char *s, size_t maxlen)
{
	size_t i, len;

	if (!s || !s[0])
		return;

	len = strlen(s);
	if (len >= maxlen)
		len = maxlen - 1;
	s[len] = '\0';

	for (i = 0; i < len; i++) {
		char c = s[i];
		if ((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') ||
		    c == '-' || c == '_') {
			continue; /* safe */
		}
		if (c == '.' && i > 0 && s[i - 1] != '.') {
			continue; /* dot allowed if not leading and not ".." */
		}
		s[i] = '_'; /* replace unsafe char */
	}

	/* prevent leading dot (hidden file / path traversal) */
	if (s[0] == '.')
		s[0] = '_';

	/* prevent empty result */
	if (s[0] == '\0') {
		s[0] = '_';
		s[1] = '\0';
	}
}

/*
 * Check if a name is safe for use in paths and shell commands.
 * Returns 1 if safe, 0 if not.
 */
int
is_safe_name(const char *s)
{
	size_t i, len;

	if (!s || !s[0])
		return 0;

	len = strlen(s);

	/* reject leading dot */
	if (s[0] == '.')
		return 0;

	for (i = 0; i < len; i++) {
		char c = s[i];
		if ((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') ||
		    c == '-' || c == '_')
			continue;
		if (c == '.' && i > 0 && s[i - 1] != '.')
			continue;
		return 0; /* unsafe character */
	}

	return 1;
}
