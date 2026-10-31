/* toolcall_parser.h -- parser + response serializer for the Gemma 4 native
 * function-calling protocol (style: ds4, small and explicit).
 *
 * Spec source: https://ai.google.dev/gemma/docs/capabilities/text/function-calling-gemma4
 * See docs/protocols/gemma4-tool-calling.md for the full grammar and
 * docs/protocols/parser-notes.md for the edge-case decisions.
 *
 * Contract, in short:
 *  - tc_parse() NEVER invents data: what it cannot interpret it reports in
 *    the 'issues' list and, per call, in call->ok / call->problem, keeping
 *    the best-effort extracted fields (never NULL).
 *  - call->args is the RAW brace payload, preserved verbatim;
 *    args pairs may be quoted (<|"|>...<|"|>) or bare.
 *  - Ownership: the tc_result owns every string it returns; free with
 *    tc_result_free().
 */
#ifndef TOOLCALL_PARSER_H
#define TOOLCALL_PARSER_H

#include "forge_util.h"

typedef struct tc_arg {
    char *key;
    char *val;
    int   quoted;  /* 1 = value came wrapped in <|"|> ... <|"|> */
} tc_arg;

typedef struct tc_call {
    char    *name;      /* function name, may be best-effort when ok==0 */
    char    *args_raw;  /* verbatim content between '{' and '<tool_call|>' */
    tc_arg  *args;
    int      n_args;
    int      ok;        /* 1 = fully well-formed */
    char    *problem;   /* "" when ok==1, human-readable reason else */
} tc_call;

typedef struct tc_result {
    tc_call *calls;        /* extracted calls (best-effort included)      */
    int      n;
    int      cap;
    char   **issues;       /* global parse diagnostics (owned strings)    */
    int      n_issues;
    int      cap_issues;
    char    *raw;          /* the WHOLE input, preserved for log/audit    */
} tc_result;

/* Parse model output for tool calls. Always succeeds as a function: whatever
 * could not be parsed ends up in issues/call->problem. Never fuzzes the
 * graveyard of the input: call->args_raw is verbatim. */
void tc_parse(const char *output, tc_result *out);

void tc_result_free(tc_result *out);

/* Find an argument by key; NULL if absent. */
const tc_arg *tc_call_arg(const tc_call *c, const char *key);

/* --- tool_response serializer (same format, host -> model side) ---------- */

/* Append one tool_response element to 'out':
 *     response:NAME{KEY:value,KEY:<|"|>value<|"|>,...}
 * Keys/vals are parallel arrays of nv elements; value i is quoted when
 * quoteds[i] != 0, else emitted bare (numbers, bools, enums). Refuses
 * (returns -1, appends nothing) if a quoted value would embed the delimiter
 * <|"|> itself, if name is empty, or on OOM.
 */
int tc_response_add(fbuf *out, const char *name, int nv,
                    const char *const *keys, const char *const *vals,
                    const int *quoteds);

/* Append  <|"|>value<|"|>  verbatim if 'value' contains no <|"|>; else -1. */
int tc_quote_value(fbuf *out, const char *value);

#endif /* TOOLCALL_PARSER_H */
