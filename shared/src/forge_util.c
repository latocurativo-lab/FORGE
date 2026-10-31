/* forge_util.c -- see forge_util.h */
#include "forge_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fb_init(fbuf *b)
{
    b->data = NULL; b->len = 0; b->cap = 0;
}

void fb_free(fbuf *b)
{
    free(b->data);
    b->data = NULL; b->len = 0; b->cap = 0;
}

void fb_clear(fbuf *b)
{
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

int fb_reserve(fbuf *b, size_t extra)
{
    if (b->len + extra <= b->cap) return 0;
    size_t want = b->cap ? b->cap : 64;
    while (want < b->len + extra) want *= 2;
    char *p = realloc(b->data, want);
    if (!p) return -1;
    b->data = p;
    b->cap = want;
    return 0;
}

int fb_append_n(fbuf *b, const char *s, size_t n)
{
    if (!s || n == 0) {
        /* still maintain the NUL guarantee for an empty buffer */
        if (fb_reserve(b, 1) != 0) return -1;
        b->data[b->len] = '\0';
        return 0;
    }
    if (fb_reserve(b, n + 1) != 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

int fb_append(fbuf *b, const char *s)
{
    if (!b) return -1;
    return s && *s ? fb_append_n(b, s, strlen(s)) : fb_append_n(b, "", 0);
}

int fb_append_c(fbuf *b, char c)
{
    if (fb_reserve(b, 2) != 0) return -1;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
    return 0;
}

int fb_appendf(fbuf *b, const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { va_end(ap2); return -1; }
    if (fb_reserve(b, (size_t)need + 1) != 0) { va_end(ap2); return -1; }
    vsnprintf(b->data + b->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
    return 0;
}

char *fstrdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *fstrndup(const char *s, size_t n)
{
    if (!s) return NULL;
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}
