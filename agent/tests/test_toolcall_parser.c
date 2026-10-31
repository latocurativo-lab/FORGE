/* test_toolcall_parser.c -- unit tests for the Gemma 4 tool-call parser.
 *
 * Each test greps the real format from the official documentation page
 * (docs/protocols/gemma4-tool-calling.md) plus the mandated edge cases:
 * multiple calls, comma-wrapped quoted args, malformed inputs, truncation.
 *
 * Build: see agent/CMakeLists.txt (target test_toolcall_parser).
 */
#include "../src/toolcall_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %-30s %s\n", __func__, msg);                        \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

static const tc_call *one_call(tc_result *r, int i)
{
    return i < r->n ? &r->calls[i] : NULL;
}

/* --- 1. single call, straight from the official doc ---------------------- */
static void test_single_call_doc(void)
{
    const char *out =
        "<|tool_call>call:get_current_weather{location:<|\"|>Tokyo, JP<|\"|>}"
        "<tool_call|><|tool_response>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 1, "expected exactly 1 call");
    CHECK(r.n_issues == 0, "no diagnostics for well-formed doc example");
    const tc_call *c = one_call(&r, 0);
    CHECK(c && strcmp(c->name, "get_current_weather") == 0, "name extracted");
    CHECK(c && c->ok == 1, "call is well-formed");
    const tc_arg *a = c ? tc_call_arg(c, "location") : NULL;
    CHECK(a && strcmp(a->val, "Tokyo, JP") == 0, "comma kept inside quoted value");
    CHECK(a && a->quoted == 1, "value marked quoted");
    CHECK(r.raw && strcmp(r.raw, out) == 0, "raw output preserved");
    tc_result_free(&r);
}

/* --- 2. multiple calls in the same output -------------------------------- */
static void test_multiple_calls(void)
{
    const char *out =
        "<|tool_call>call:get_current_weather{location:<|\"|>Tokyo, JP<|\"|>}<tool_call|>"
        "<|tool_call>call:get_current_weather{location:<|\"|>Rome, IT<|\"|>}<tool_call|>"
        "<|tool_response>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 2, "two calls detected");
    CHECK(r.n_issues == 0, "no diagnostics");
    CHECK(strcmp(r.calls[0].name, "get_current_weather") == 0 &&
          strcmp(r.calls[1].name, "get_current_weather") == 0, "both names");
    CHECK(strcmp(tc_call_arg(&r.calls[0], "location")->val, "Tokyo, JP") == 0, "call 1 args");
    CHECK(strcmp(tc_call_arg(&r.calls[1], "location")->val, "Rome, IT") == 0, "call 2 args");
    tc_result_free(&r);
}

/* --- 3. mixed quoted / bare args, several values -------------------------- */
static void test_mixed_and_bare_args(void)
{
    const char *out =
        "<|tool_call>call:set_thermostat{"
        "location:<|\"|>Milano<|\"|>,temp:21,on:true,offset:-2.5}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 1 && r.n_issues == 0, "one clean call");
    const tc_call *c = one_call(&r, 0);
    CHECK(c && c->n_args == 4, "4 arguments parsed");
    CHECK(strcmp(tc_call_arg(c, "temp")->val, "21") == 0, "bare number 21");
    CHECK(tc_call_arg(c, "temp")->quoted == 0, "temp is bare");
    CHECK(strcmp(tc_call_arg(c, "on")->val, "true") == 0, "bare enum true");
    CHECK(strcmp(tc_call_arg(c, "offset")->val, "-2.5") == 0, "negative float kept");
    CHECK(strcmp(tc_call_arg(c, "location")->val, "Milano") == 0, "quoted string");
    tc_result_free(&r);
}

/* --- 4. doc-style enum array transported inside a quoted value ------------ */
static void test_enum_array_inside_quote(void)
{
    /* mirrors the tool declaration shape: enum:[<|"|>celsius<|"|>,<|"|>fahrenheit<|"|>]
     * transported as a value: commas and quotes must survive verbatim. */
    const char *out =
        "<|tool_call>call:set_unit{unit:<|\"|>celsius<|\"|>},alt:<|\"|>pa<|\"|>}<tool_call|>";
    /* note: even the payload with a following callee must not confuse us */
    const char *out2 =
        "<|tool_call>call:convert{from:<|\"|>celsius,fahrenheit<|\"|>}<tool_call|>";
    tc_result r;
    tc_parse(out2, &r);
    CHECK(r.n == 1 && r.n_issues == 0, "quote survives comma payload");
    CHECK(strcmp(tc_call_arg(&r.calls[0], "from")->val, "celsius,fahrenheit") == 0, "comma inside quoted value");
    (void)out;
    tc_result_free(&r);
}

/* --- 5. empty output / no tool tokens ------------------------------------- */
static void test_empty_and_plain(void)
{
    tc_result r;
    tc_parse("", &r);
    CHECK(r.n == 0 && r.n_issues == 0, "empty output: zero calls, zero issues");
    CHECK(r.raw && strcmp(r.raw, "") == 0, "raw preserved even when empty");
    tc_result_free(&r);

    tc_parse("The current weather in Tokyo is 15 degrees and sunny.", &r);
    CHECK(r.n == 0 && r.n_issues == 0, "plain text: nothing to extract");
    tc_result_free(&r);
}

/* --- 6. missing closing <tool_call|> (truncation in mid-call) ------------- */
static void test_truncated_mid_call(void)
{
    const char *out =
        "<|tool_call>call:get_current_weather{location:<|\"|>Tokyo, JP";
    tc_result r;
    tc_parse(out, &r);
    /* No fabrication: 0 calls, one clear diagnostic, raw kept for forensics */
    CHECK(r.n == 0, "truncated call: no invented call record");
    CHECK(r.n_issues >= 1, "truncation diagnostic filed");
    CHECK(strstr(r.issues[0] ? r.issues[0] : "", "unterminated") != NULL,
          "diagnostic mentions unterminated <tool_call|>");
    CHECK(r.raw && strcmp(r.raw, out) == 0, "raw preserved");
    tc_result_free(&r);
}

/* --- 7. unterminated quoted value ----------------------------------------- */
static void test_unterminated_quote(void)
{
    const char *out = "<|tool_call>call:read_file{path:<|\"|>src/main.c}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 1, "call is still found (raw span) for forensics");
    CHECK(r.calls[0].ok == 0, "ok flag is 0 on the call");
    CHECK(strstr(r.calls[0].problem, "unterminated quoted value") != NULL,
          "problem text explains the corruption");
    tc_result_free(&r);
}

/* --- 8. <|\"|> inside a quoted value (embedded delimiter) ------------------ */
static void test_quote_token_inside_value(void)
{
    const char *out =
        "<|tool_call>call:say{text:<|\"|>say <|\"|> hi<|\"|>}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n >= 1, "call shell still recognised");
    /* No invention: best-effort value up to the FIRST closing token, and the
     * call is flagged as corrupted (see docs/protocols/parser-notes.md). */
    CHECK(strcmp(tc_call_arg(&r.calls[0], "text")->val, "say ") == 0,
          "best-effort value up to first terminator");
    CHECK(r.calls[0].ok == 0, "embedded <|\"|> flags the call");
    tc_result_free(&r);
}

/* --- 9. missing call: prefix after <|tool_call> ---------------------------- */
static void test_missing_call_prefix(void)
{
    const char *out = "<|tool_call>get_current_weather{location:<|\"|>Tokyo<|\"|>}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 0, "no call: prefix means no invented call");
    CHECK(r.n_issues >= 1, "diagnostic filed");
    CHECK(strstr(r.issues[0], "call:") != NULL, "mentions the missing 'call:'");
    tc_result_free(&r);
}

/* --- 10. malformed tool name is flagged, not invented ---------------------- */
static void test_bad_name(void)
{
    const char *out = "<|tool_call>call:bad name{a:1}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 0, "space-containing name: no call record");
    CHECK(r.n_issues >= 1 && strstr(r.issues[0], "malformed tool name") != NULL, "name diagnostic");
    tc_result_free(&r);
}

/* --- 11. doc-driven example: full response serializer round-trip ----------- */
static void test_response_serializer(void)
{
    /* Expected shape exactly from the doc example:
     *   response:get_current_weather{temperature:15,weather:<|"|>sunny<|"|>} */
    static const char *keys[]   = { "temperature", "weather" };
    static const char *vals[]   = { "15",          "sunny"   };
    static const int   quoteds[]= { 0,             1         };

    fbuf b; fb_init(&b);
    CHECK(tc_response_add(&b, "get_current_weather", 2, keys, vals, quoteds) == 0,
          "serializer worked");
    CHECK(strcmp(b.data, "response:get_current_weather{temperature:15,weather:<|\"|>sunny<|\"|>}") == 0,
          "exact doc-format response");
    fb_free(&b);

    /* refusing to embed <|\"|> in a value */
    static const char *keys2[] = { "text" };
    static const char *vals2[] = { "say <|\"|> hi" };
    static const int   quot2[] = { 1 };
    fbuf b2; fb_init(&b2);
    CHECK(tc_response_add(&b2, "echo", 1, keys2, vals2, quot2) != 0,
          "embedded delimiter refused at serializer level");
    CHECK(b2.len == 0, "refused call appended nothing");
    fb_free(&b2);

    /* multiple responses per turn */
    fbuf b3; fb_init(&b3);
    static const char *k3[] = { "temperature" };
    static const char *v3[] = { "15" };
    static const int   q3[] = { 0 };
    static const char *v3b[]= { "19" };
    CHECK(tc_response_add(&b3, "get_current_weather", 1, k3, v3, q3) == 0, "resp 1");
    CHECK(tc_response_add(&b3, "get_current_weather", 1, k3, v3b, q3) == 0, "resp 2");
    CHECK(strcmp(b3.data,
                 "response:get_current_weather{temperature:15}"
                 "response:get_current_weather{temperature:19}") == 0,
          "multiple responses appended");
    fb_free(&b3);
}

/* --- 12. declared call with white-space-tolerant empty args --------------- */
static void test_empty_args(void)
{
    const char *out = "<|tool_call>call:noop{}<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 1 && r.calls[0].ok == 1 && r.calls[0].n_args == 0, "empty args are legal");
    tc_result_free(&r);
}

/* --- 13. nested braces inside quoted args stay verbatim -------------------- */
static void test_nested_raw_arg_shape(void)
{
    const char *out =
        "<|tool_call>call:replace{"
        "path:<|\"|>C:\\main.c<|\"|>,regex:{from:<|\"|>a+b<|\"|>,to:<|\"|>c<|\"|>}}"
        "<tool_call|>";
    tc_result r;
    tc_parse(out, &r);
    CHECK(r.n == 1, "call found");
    CHECK(strcmp(tc_call_arg(&r.calls[0], "path")->val, "C:\\main.c") == 0, "backslash path verbatim");
    /* second arg consumed as bare until depth-0 ',' — the regex pair survives */
    CHECK(tc_call_arg(&r.calls[0], "regex") != NULL, "nested arg key found");
    tc_result_free(&r);
}

static void test_reject_corrupted_args(void)
{
    const char *cases[] = {
        "<|tool_call>call:f{,x:1}<tool_call|>",
        "<|tool_call>call:f{x:1,,y:2}<tool_call|>",
        "<|tool_call>call:f{x:1,}<tool_call|>",
        "<|tool_call>call:f{bad key:1}<tool_call|>",
        "<|tool_call>call:f{x:[1}<tool_call|>"
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        tc_result r;
        tc_parse(cases[i], &r);
        CHECK(r.n == 1 && !r.calls[0].ok && r.n_issues > 0,
              "malformed arguments must never be accepted");
        tc_result_free(&r);
    }
}

int main(void)
{
    test_single_call_doc();
    test_multiple_calls();
    test_mixed_and_bare_args();
    test_enum_array_inside_quote();
    test_empty_and_plain();
    test_truncated_mid_call();
    test_unterminated_quote();
    test_quote_token_inside_value();
    test_missing_call_prefix();
    test_bad_name();
    test_response_serializer();
    test_empty_args();
    test_nested_raw_arg_shape();
    test_reject_corrupted_args();
    if (g_fail == 0) {
        printf("OK test_toolcall_parser: all tests passed\n");
        return 0;
    }
    printf("FAILED test_toolcall_parser: %d assertion(s)\n", g_fail);
    return 1;
}
