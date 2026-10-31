/* forge_json.c -- minimal JSON; see forge_json.h for scope and contract. */
#include "forge_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fj_value *fj_new(fj_type t)
{
    fj_value *v = calloc(1, sizeof(fj_value));
    if (v) v->type = t;
    return v;
}

/* --- read access -------------------------------------------------------- */

const fj_value *fjson_get(const fj_value *obj, const char *key)
{
    if (!obj || obj->type != FJ_OBJECT || !key) return NULL;
    for (const fj_value *c = obj->head; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

size_t fjson_count(const fj_value *v)
{
    if (!v || (v->type != FJ_ARRAY && v->type != FJ_OBJECT)) return 0;
    size_t n = 0;
    for (const fj_value *c = v->head; c; c = c->next) n++;
    return n;
}

const fj_value *fjson_at(const fj_value *arr, size_t i)
{
    if (!arr || arr->type != FJ_ARRAY) return NULL;
    const fj_value *c = arr->head;
    for (size_t k = 0; k < i && c; k++) c = c->next;
    return c;
}

double fjson_num(const fj_value *v)
{
    return v && v->type == FJ_NUMBER ? v->num : 0.0;
}

const char *fjson_str(const fj_value *v)
{
    return v && v->type == FJ_STRING ? v->str : "";
}

int fjson_bool(const fj_value *v)
{
    return v && v->type == FJ_BOOL ? v->bval : 0;
}

/* --- building ----------------------------------------------------------- */

fj_value *fjson_object_new(void) { return fj_new(FJ_OBJECT); }
fj_value *fjson_array_new(void)  { return fj_new(FJ_ARRAY);  }

fj_value *fjson_string_new(const char *s)
{
    fj_value *v = fj_new(FJ_STRING);
    if (v) v->str = fstrdup(s ? s : "");
    return v;
}

fj_value *fjson_number_new(double n)
{
    fj_value *v = fj_new(FJ_NUMBER);
    if (v) v->num = n;
    return v;
}

fj_value *fjson_bool_new(int b)
{
    fj_value *v = fj_new(FJ_BOOL);
    if (v) v->bval = !!b;
    return v;
}

fj_value *fjson_null_new(void)
{
    return fj_new(FJ_NULL);
}

static fj_value *fj_container_last(fj_value *c)
{
    fj_value *it = c->head;
    if (!it) return NULL;
    while (it->next) it = it->next;
    return it;
}

int fjson_object_set(fj_value *obj, const char *key, fj_value *val)
{
    if (!obj || obj->type != FJ_OBJECT || !key || !val) { fjson_free(val); return -1; }
    char *k = fstrdup(key);
    if (!k) { fjson_free(val); return -1; }
    free(val->key);
    val->key = k;
    fj_value *last = fj_container_last(obj);
    if (last) last->next = val; else obj->head = val;
    return 0;
}

int fjson_array_push(fj_value *arr, fj_value *val)
{
    if (!arr || arr->type != FJ_ARRAY || !val) { fjson_free(val); return -1; }
    fj_value *last = fj_container_last(arr);
    if (last) last->next = val; else arr->head = val;
    return 0;
}

void fjson_free(fj_value *v)
{
    if (!v) return;
    while (v->head) {
        fj_value *c = v->head;
        v->head = c->next;
        fjson_free(c);
    }
    free(v->str);
    free(v->key);
    free(v);
}

/* --- writer ------------------------------------------------------------- */

static int fj_write_escaped(const char *s, fbuf *out)
{
    if (fb_append_c(out, '"') != 0) return -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char c = (char)*p;
        const char *esc = NULL;
        switch (c) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n";  break;
        case '\r': esc = "\\r";  break;
        case '\t': esc = "\\t";  break;
        case '\b': esc = "\\b";  break;
        case '\f': esc = "\\f";  break;
        default: break;
        }
        int named = NULL != esc;
        if (!esc && (unsigned char)c < 0x20) {
            if (fb_appendf(out, "\\u%04x", (unsigned char)c) != 0) return -1;
        } else if (named) {
            if (fb_append(out, esc) != 0) return -1;
        } else {
            if (fb_append_c(out, c) != 0) return -1;
        }
    }
    return fb_append_c(out, '"');
}

int fjson_write(const fj_value *v, fbuf *out)
{
    if (!v || !out) return -1;
    switch (v->type) {
    case FJ_NULL:   return fb_append(out, "null");
    case FJ_BOOL:   return fb_append(out, v->bval ? "true" : "false");
    case FJ_NUMBER: return fb_appendf(out, "%.17g", v->num);
    case FJ_STRING: return fj_write_escaped(v->str, out);
    case FJ_ARRAY:
        if (fb_append_c(out, '[') != 0) return -1;
        for (const fj_value *c = v->head; c; c = c->next) {
            if (fjson_write(c, out) != 0) return -1;
            if (c->next && fb_append_c(out, ',') != 0) return -1;
        }
        return fb_append_c(out, ']');
    case FJ_OBJECT:
        if (fb_append_c(out, '{') != 0) return -1;
        for (const fj_value *c = v->head; c; c = c->next) {
            if (fj_write_escaped(c->key ? c->key : "", out) != 0) return -1;
            if (fb_append_c(out, ':') != 0) return -1;
            if (fjson_write(c, out) != 0) return -1;
            if (c->next && fb_append_c(out, ',') != 0) return -1;
        }
        return fb_append_c(out, '}');
    }
    return -1;
}

/* --- parser ------------------------------------------------------------- */

typedef struct {
    const char *s;
    size_t pos, len;
} fj_parser;

static void fj_skip_ws(fj_parser *P)
{
    while (P->pos < P->len && isspace((unsigned char)P->s[P->pos])) P->pos++;
}

static int fj_peek(fj_parser *P)
{
    return P->pos < P->len ? (unsigned char)P->s[P->pos] : -1;
}

static int fj_expect(fj_parser *P, char c)
{
    if (fj_peek(P) != (unsigned char)c) return -1;
    P->pos++;
    return 0;
}

static fj_value *fj_fail(fj_parser *P, fj_value *partial)
{
    (void)P;
    fjson_free(partial);
    return NULL;
}

/* Parse \uXXXX, optionally followed by a low surrogate, emit UTF-8. */
static int fj_parse_unicode(fj_parser *P, fbuf *out)
{
    unsigned cp = 0;
    for (int i = 0; i < 4; i++) {
        int c = fj_peek(P);
        unsigned v;
        if (c >= '0' && c <= '9')      v = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (unsigned)(c - 'A' + 10);
        else return -1;
        P->pos++;
        cp = cp * 16 + v;
    }
    if (cp >= 0xD800 && cp <= 0xDBFF && P->s[P->pos] == '\\' && P->s[P->pos + 1] == 'u') {
        P->pos += 2;
        unsigned lo = 0;
        int okc = 1;
        for (int i = 0; i < 4; i++) {
            int c = fj_peek(P);
            unsigned v;
            if (c >= '0' && c <= '9')      v = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = (unsigned)(c - 'A' + 10);
            else { okc = 0; break; }
            P->pos++;
            lo = lo * 16 + v;
        }
        if (okc && lo >= 0xDC00 && lo <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        else return -1; /* lone/bad surrogate in the source is invalid JSON */
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
        return -1; /* lone surrogate */
    }
    char enc[4];
    size_t n = 0;
    if (cp < 0x80)        enc[n++] = (char)cp;
    else if (cp < 0x800)  { enc[n++] = (char)(0xC0 | (cp >> 6));  enc[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000){ enc[n++] = (char)(0xE0 | (cp >> 12)); enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[n++] = (char)(0x80 | (cp & 0x3F)); }
    else                  { enc[n++] = (char)(0xF0 | (cp >> 18)); enc[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); enc[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[n++] = (char)(0x80 | (cp & 0x3F)); }
    return fb_append_n(out, enc, n);
}

static int fj_parse_string_raw(fj_parser *P, fbuf *out)
{
    if (fj_expect(P, '"') != 0) return -1;
    while (P->pos < P->len) {
        char c = P->s[P->pos++];
        if (c == '"') return 0;
        if (c == '\\') {
            if (P->pos >= P->len) return -1;
            char cc = P->s[P->pos++];
            switch (cc) {
            case '"':  if (fb_append_c(out, '"') != 0) return -1; break;
            case '\\': if (fb_append_c(out, '\\') != 0) return -1; break;
            case '/':  if (fb_append_c(out, '/') != 0) return -1; break;
            case 'b':  if (fb_append_c(out, '\b') != 0) return -1; break;
            case 'f':  if (fb_append_c(out, '\f') != 0) return -1; break;
            case 'n':  if (fb_append_c(out, '\n') != 0) return -1; break;
            case 'r':  if (fb_append_c(out, '\r') != 0) return -1; break;
            case 't':  if (fb_append_c(out, '\t') != 0) return -1; break;
            case 'u':  if (fj_parse_unicode(P, out) != 0) return -1; break;
            default:   return -1;
            }
        } else {
            if (fb_append_c(out, c) != 0) return -1;
        }
    }
    return -1; /* unterminated string */
}

static fj_value *fj_parse_value(fj_parser *P);

static int fj_parse_children(fj_parser *P, fj_value *cont, char close, int object)
{
    fj_skip_ws(P);
    if (fj_peek(P) == (unsigned char)close) { P->pos++; return 0; }
    for (;;) {
        fj_skip_ws(P); /* new key/member may follow any whitespace */
        char *key = NULL;
        if (object) {
            fbuf kb; fb_init(&kb);
            if (fj_parse_string_raw(P, &kb) != 0) { fb_free(&kb); return -1; }
            key = fstrndup(kb.data ? kb.data : "", kb.len);
            fb_free(&kb);
            if (!key) return -1;
            fj_skip_ws(P);
            if (fj_expect(P, ':') != 0) { free(key); return -1; }
        }
        fj_value *child = fj_parse_value(P);
        if (!child) { free(key); return -1; }
        if (object) { free(child->key); child->key = key; }
        fj_value *last = fj_container_last(cont);
        if (last) last->next = child; else cont->head = child;
        fj_skip_ws(P);
        int c = fj_peek(P);
        if (c == (unsigned char)close) { P->pos++; return 0; }
        if (c != ',') return -1;
        P->pos++;
    }
}

static fj_value *fj_parse_value(fj_parser *P)
{
    fj_skip_ws(P);
    int c = fj_peek(P);
    if (c < 0) return NULL;

    if (c == '{') {
        P->pos++;
        fj_value *o = fj_new(FJ_OBJECT);
        if (!o) return NULL;
        if (fj_parse_children(P, o, '}', 1) != 0) return fj_fail(P, o);
        return o;
    }
    if (c == '[') {
        P->pos++;
        fj_value *a = fj_new(FJ_ARRAY);
        if (!a) return NULL;
        if (fj_parse_children(P, a, ']', 0) != 0) return fj_fail(P, a);
        return a;
    }
    if (c == '"') {
        fbuf sb; fb_init(&sb);
        if (fj_parse_string_raw(P, &sb) != 0) { fb_free(&sb); return NULL; }
        fj_value *v = fj_new(FJ_STRING);
        if (!v) { fb_free(&sb); return NULL; }
        v->str = fstrndup(sb.data ? sb.data : "", sb.len);
        fb_free(&sb);
        if (!v->str) { fjson_free(v); return NULL; }
        return v;
    }
    if (c == 't') {
        if (strncmp(P->s + P->pos, "true", 4) != 0) return NULL;
        P->pos += 4;
        return fjson_bool_new(1);
    }
    if (c == 'f') {
        if (strncmp(P->s + P->pos, "false", 5) != 0) return NULL;
        P->pos += 5;
        return fjson_bool_new(0);
    }
    if (c == 'n') {
        if (strncmp(P->s + P->pos, "null", 4) != 0) return NULL;
        P->pos += 4;
        return fjson_null_new();
    }
    /* number */
    {
        const char *start = P->s + P->pos;
        char *end = NULL;
        double n = strtod(start, &end);
        if (end == start) return NULL;
        P->pos += (size_t)(end - start);
        return fjson_number_new(n);
    }
}

fj_value *fjson_parse(const char *text)
{
    if (!text) return NULL;
    fj_parser P = { text, 0, strlen(text) };
    fj_value *v = fj_parse_value(&P);
    if (!v) return NULL;
    fj_skip_ws(&P);
    if (P.pos != P.len) { fjson_free(v); return NULL; } /* trailing garbage */
    return v;
}
