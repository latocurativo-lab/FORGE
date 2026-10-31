/* toolcall_parser.c -- see toolcall_parser.h for contract.
 *
 * Grammar being parsed (host side, model output):
 *   open  = "<|tool_call>"
 *   call  = "call:" NAME "{" ARGS "}" "<tool_call|>"
 *   arg   = KEY ":" ( QUOTED | BARE )
 *   QUOTED= "<|\"|>" ...first following "<|\"|>"...   (no escapes in format)
 *   BARE  = chars until top-level ','; braces/brackets tracked for depth
 *
 * The reference regex on the official doc is equivalent to ours for the
 * non-nested path; we add structural checks it silently skips (that is the
 * point of tal: no invention, every anomaly goes to 'issues').
 */
#include "toolcall_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P_K_OPEN_CALL   "<|tool_call>"
#define P_K_CALL        "call:"
#define P_K_CLOSE_CALL  "}<tool_call|>"
#define P_K_QUOTE       "<|\"|>"
#define P_K_QUOTE_LEN   5   /* strlen('<|"|>'): < | " | > (the \" is ONE byte) */

static const char P_K_END_QUOTED = ',';

void tc_result_free(tc_result *out)
{
    if (!out) return;
    for (int i = 0; i < out->n; i++) {
        tc_call *c = &out->calls[i];
        for (int j = 0; j < c->n_args; j++)
            { free(c->args[j].key); free(c->args[j].val); }
        free(c->args);
        free(c->name);
        free(c->args_raw);
        free(c->problem);
    }
    free(out->calls);
    for (int i = 0; i < out->n_issues; i++) free(out->issues[i]);
    free(out->issues);
    free(out->raw);
    memset(out, 0, sizeof(*out));
}

const tc_arg *tc_call_arg(const tc_call *c, const char *key)
{
    if (!c || !key) return NULL;
    for (int i = 0; i < c->n_args; i++)
        if (strcmp(c->args[i].key, key) == 0) return &c->args[i];
    return NULL;
}

static void issue(tc_result *out, const char *what)
{
    char *d = fstrdup(what);
    if (!d) return; /* OOM: diagnostic lost, calls stay consistent */
    if (out->n_issues == out->cap_issues) {
        int nc = out->cap_issues ? out->cap_issues * 2 : 8;
        char **p = realloc(out->issues, (size_t)nc * sizeof(char *));
        if (!p) { free(d); return; }
        out->issues = p;
        out->cap_issues = nc;
    }
    out->issues[out->n_issues++] = d;
}

static void call_problem(tc_result *out, tc_call *c, const char *what)
{
    c->ok = 0;
    char buf[320];
    snprintf(buf, sizeof(buf), "call '%s': %s", c->name ? c->name : "?", what);
    issue(out, buf);
    free(c->problem);
    c->problem = fstrdup(what);
}

static int call_push(tc_result *out, const char *name, size_t nlen, const char *args, size_t alen)
{
    if (out->n == out->cap) {
        int nc = out->cap ? out->cap * 2 : 8;
        tc_call *p = realloc(out->calls, (size_t)nc * sizeof(tc_call));
        if (!p) return -1;
        out->calls = p;
        out->cap = nc;
    }
    tc_call *c = &out->calls[out->n];
    memset(c, 0, sizeof(*c));
    c->name = fstrndup(name, nlen);
    c->args_raw = fstrndup(args, alen);
    c->problem = fstrdup("");
    c->ok = 1;
    if (!c->name || !c->args_raw || !c->problem) {
        free(c->name); free(c->args_raw); free(c->problem);
        return -1;
    }
    out->n++;
    return 0;
}

/* depth state for BARE values: {}, [], () may nest, ',' stops only at depth 0 */
typedef struct { int br[3]; } depth;
static void depth_add(depth *d, char c)
{
    switch (c) {
    case '{': d->br[0]++; break;
    case '[': d->br[1]++; break;
    case '(': d->br[2]++; break;
    case '}': d->br[0]--; break;
    case ']': d->br[1]--; break;
    case ')': d->br[2]--; break;
    default: break;
    }
}

static int depth_ok(depth *d)
{
    for (int i = 0; i < 3; i++) {
        if (d->br[i] == 0) continue;
        if (d->br[i] > 0) return 1;   /* open and unmatched = still on one value */
        return -1;                    /* too many closers = corruption */
    }
    return 0;
}

static tc_arg *arg_push(tc_call *c)
{
    tc_arg *p = realloc(c->args, (size_t)(c->n_args + 1) * sizeof(tc_arg));
    if (!p) return NULL;
    c->args = p;
    tc_arg *a = &c->args[c->n_args++];
    memset(a, 0, sizeof(*a));
    return a;
}

static char *trimndup(const char *s, size_t n)
{
    while (n > 0 && s[0] == ' ')      { s++; n--; }
    while (n > 0 && s[n - 1] == ' ')  { n--; }
    return fstrndup(s, n);
}

static int valid_name(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.'))
            return 0;
    }
    return n > 0;
}

/* Parse one ARGS slice ('{'-to-'<' content). Never aborts: whatever cannot
 * be understood becomes a problem note on the call itself. */
static void parse_args(tc_result *out, tc_call *c)
{
    char raw_ok = 1;
    const char *p = c->args_raw;
    const char *end = p + strlen(p);

    if (*p == '\0') return; /* empty args, legal (call:name{}) */

    for (;;) {
        /* A separator is consumed after each value. Additional commas are
         * malformed, not whitespace to silently recover from. */
        while (*p == ' ') p++;
        if (p >= end) break;
        if (*p == ',') {
            call_problem(out, c, "unexpected argument separator");
            break;
        }

        /* KEY up to ':' */
        const char *ks = p;
        while (p < end && *p != ':' && *p != ',') p++;
        if (p >= end || *p == ',') {
            call_problem(out, c, "argument key without ':' ");
            raw_ok = 0;
            break;
        }
        char *key = trimndup(ks, (size_t)(p - ks));
        if (!key || !valid_name(key, key ? strlen(key) : 0)) {
            free(key);
            call_problem(out, c, "invalid argument key");
            break;
        }
        p++; /* past ':' */

        /* VALUE */
        char *val = NULL;
        int quoted = 0;
        if (strncmp(p, P_K_QUOTE, P_K_QUOTE_LEN) == 0) {
            p += P_K_QUOTE_LEN;
            const char *vs = p;
            const char *qe = strstr(p, P_K_QUOTE);
            if (!qe || qe >= end) {
                call_problem(out, c, "unterminated quoted value (no closing <|\"|>)");
                raw_ok = 0;
                free(key);
                break; /* rest of args stays in args_raw for forensics */
            }
            val = fstrndup(vs, (size_t)(qe - vs));
            quoted = 1;
            p = qe + P_K_QUOTE_LEN;
            while (*p == ' ') p++;
            if (p < end) {
                if (*p == P_K_END_QUOTED) p++;              /* separator, next arg */
                else {
                    call_problem(out, c, "junk after quoted value; possible embedded <|\"|> in data");
                    raw_ok = 0;
                    while (p < end && *p != P_K_END_QUOTED) p++; /* resync */
                    if (p < end) p++;
                }
            }
        } else { /* BARE */
            depth d = {0};
            const char *vs = p;
            while (p < end) {
                depth_add(&d, *p);
                if (depth_ok(&d) < 0) break;                 /* over-close: broken */
                if (*p == P_K_END_QUOTED && depth_ok(&d) == 0) break;
                p++;
            }
            if (depth_ok(&d) != 0) {
                call_problem(out, c, "unbalanced brackets in bare value");
                raw_ok = 0;
                free(key);
                break;
            }
            /* a bare value that itself embeds a call-like '<|' token is data
             * corruption: do NOT invent a split; flag it */
            if (memchr(vs, '<', (size_t)(p - vs)) || memchr(vs, '"', (size_t)(p - vs))) {
                call_problem(out, c, "bare value contains token-like characters (< or \")");
            }
            val = trimndup(vs, (size_t)(p - vs));
            if (!val) { free(key); call_problem(out, c, "out of memory"); break; }
            if (*val == '\0') { call_problem(out, c, "empty bare value"); raw_ok = 0; }
            if (p < end) p++; /* past ',' */
        }

        tc_arg *a = val ? arg_push(c) : NULL;
        if (a) {
            a->key = key;
            a->val = val;
            a->quoted = quoted;
        } else {
            free(key);
            free(val);
            raw_ok = 0;
        }

        if (p >= end) {
            if (end > c->args_raw && end[-1] == ',')
                call_problem(out, c, "trailing argument separator");
            break;
        }
    }

    (void)raw_ok; /* problems were already reduced via call_problem() */
}

void tc_parse(const char *output, tc_result *out)
{
    memset(out, 0, sizeof(*out));
    const char *s = output ? output : "";
    out->raw = fstrdup(s);
    if (!out->raw) return;

    const char *curs = s;
    for (;;) {
        const char *open = strstr(curs, P_K_OPEN_CALL);
        if (!open) break;

        const char *after = open + strlen(P_K_OPEN_CALL);
        if (strncmp(after, P_K_CALL, 5) != 0) {
            issue(out, "<|tool_call> without 'call:' prefix");
            curs = after;
            continue;
        }

        const char *name = after + 5;                /* past 'call:' */
        const char *brace = strchr(name, '{');
        if (!brace) {
            issue(out, "tool call without opening brace '{'");
            curs = after;                            /* do not skip the call itself */
            continue;
        }

        const char *close = strstr(brace, P_K_CLOSE_CALL); /* '}' + '<tool_call|>' */
        if (!close) {
            issue(out, "unterminated tool call (missing <tool_call|>)");
            /* A truncated generation: everything after open is the partial
             * call. Do not fabricate a call record from half data -- the raw
             * buffer is already preserved in result->raw. */
            break;
        }

        size_t nlen = (size_t)(brace - name);
        if (!valid_name(name, nlen)) {
            char buf[160];
            char *nm = trimndup(name, nlen);
            snprintf(buf, sizeof(buf), "malformed tool name '%.50s'",
                     nm ? nm : name);
            free(nm);
            issue(out, buf);
            /* Corrupted name: keep the raw span for forensics but do NOT add
             * a half-fake call. Onwards from the closing token. */
            curs = close + strlen(P_K_CLOSE_CALL);
            continue;
        }

        size_t alen = (size_t)(close - brace - 1);   /* brace is '[' of args at name+nlen */
        const char *astart = brace + 1;
        if (call_push(out, name, nlen, astart, alen) != 0) break; /* OOM only */
        parse_args(out, &out->calls[out->n - 1]);

        curs = close + strlen(P_K_CLOSE_CALL);
    }
}

/* --- response serializer ------------------------------------------------ */

int tc_quote_value(fbuf *out, const char *value)
{
    if (!out || !value) return -1;
    if (strstr(value, P_K_QUOTE) != NULL) return -1; /* refused: would corrupt */
    if (fb_append(out, P_K_QUOTE) != 0) return -1;
    if (fb_append(out, value) != 0) return -1;
    if (fb_append(out, P_K_QUOTE) != 0) return -1;
    return 0;
}

int tc_response_add(fbuf *out, const char *name, int nv,
                    const char *const *keys, const char *const *vals,
                    const int *quoteds)
{
    if (!out || !name || !name[0]) return -1;

    /* build the payload on a scratch buffer first: appending '}' after a
     * caller-built payload in the same buffer cannot order-robustly work */
    fbuf pay; fb_init(&pay);
    for (int i = 0; i < nv; i++) {
        if (!keys[i] || !vals[i]) { fb_free(&pay); return -1; }
        if (i > 0 && fb_append_c(&pay, ',') != 0) { fb_free(&pay); return -1; }
        if (fb_appendf(&pay, "%s:", keys[i]) != 0) { fb_free(&pay); return -1; }
        if (quoteds && quoteds[i]) {
            if (tc_quote_value(&pay, vals[i]) != 0) { fb_free(&pay); return -1; }
        } else {
            if (fb_append(&pay, vals[i]) != 0) { fb_free(&pay); return -1; }
        }
    }
    int rc = fb_appendf(out, "response:%s{", name) != 0
             || (pay.len > 0 && fb_append(out, pay.data) != 0)
             || fb_append(out, "}") != 0 ? -1 : 0;
    fb_free(&pay);
    return rc;
}
