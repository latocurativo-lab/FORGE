/* forge_util.h -- small shared helpers for FORGE (dynamic byte buffer, strdups)
 *
 * C11, libc only. Owned by Agente C.
 */
#ifndef FORGE_UTIL_H
#define FORGE_UTIL_H

#include <stddef.h>

/* Growable byte buffer. data[len] is ALWAYS '\0' after any fb_* append, so
 * the string contents can be read directly. All fb_* functions return 0 on
 * success, -1 on allocation failure. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} fbuf;

void fb_init(fbuf *b);
void fb_free(fbuf *b);
void fb_clear(fbuf *b);
int fb_reserve(fbuf *b, size_t extra);        /* make room for 'extra' more bytes */
int  fb_append_n(fbuf *b, const char *s, size_t n);
int  fb_append(fbuf *b, const char *s);
int  fb_append_c(fbuf *b, char c);             /* appends one byte */
int  fb_appendf(fbuf *b, const char *fmt, ...);

/* NULL-tolerant strdup helpers (like the POSIX ones, but portable). */
char *fstrdup(const char *s);
char *fstrndup(const char *s, size_t n);

#endif /* FORGE_UTIL_H */
