/* test_forge_json.c -- unit tests for shared/src/forge_json (parse/build/write).
 *
 * Config-realistic object, arrays, escapes, \uXXXX, numbers, round-trip
 * write->parse->compare.
 */
#include "../../shared/src/forge_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %-28s %s\n", __func__, msg);                        \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

static void test_parse_config_like(void)
{
    const char *doc =
        "{\n"
        "  \"model\": \"gemma-4-12B-it\",\n"
        "  \"quant\": \"Q4_K_M\",\n"
        "  \"thinking\": false ,\n"
        "  \"ctx\": 16384,\n"
        "  \"limit\": 2.5e2,\n"
        "  \"tags\": [\"local\", \"gguf\", 7],\n"
        "  \"nested\": {\"a\": true, \"b\": [null, \"x\"]}\n"
        "}";

    fj_value *v = fjson_parse(doc);
    CHECK(v != NULL, "doc parses");
    if (!v) return;
    CHECK(strcmp(fjson_str(fjson_get(v, "model")), "gemma-4-12B-it") == 0, "string field");
    CHECK(strcmp(fjson_str(fjson_get(v, "quant")), "Q4_K_M") == 0, "string field 2");
    CHECK(fjson_bool(fjson_get(v, "thinking")) == 0, "bool false");
    CHECK(fjson_num(fjson_get(v, "ctx")) == 16384, "int number");
    CHECK(fjson_num(fjson_get(v, "limit")) == 250.0, "exp number");

    const fj_value *tags = fjson_get(v, "tags");
    CHECK(tags && fjson_count(tags) == 3, "array count");
    CHECK(tags && strcmp(fjson_str(fjson_at(tags, 0)), "local") == 0, "array[0]");
    CHECK(tags && fjson_num(fjson_at(tags, 2)) == 7, "array[2] number");

    const fj_value *nested = fjson_get(v, "nested");
    CHECK(nested && fjson_bool(fjson_get(nested, "a")) == 1, "nested object");
    const fj_value *b = nested ? fjson_get(nested, "b") : NULL;
    CHECK(b && b->type == FJ_ARRAY, "nested array typed");
    CHECK(b && fjson_at(b, 0) && fjson_at(b, 0)->type == FJ_NULL, "null member body");
    CHECK(b && strcmp(fjson_str(fjson_at(b, 1)), "x") == 0, "array nested string");

    fjson_free(v);
}

static void test_escapes(void)
{
    fj_value *v = fjson_parse("\"line\\n\\t\\\"q\\\" \\\\ \\u00e9 \\ud83d\\ude00 \\u0041\"");
    CHECK(v != NULL, "escaped string parses");
    CHECK(v && strcmp(v->str,
        "line\n\t\"q\" \\ \xc3\xa9 \xf0\x9f\x98\x80 A") == 0, "escapes decoded (incl. surrogate pair)");
    fjson_free(v);

    /* malformed JSON must be rejected, not fuzzed */
    CHECK(fjson_parse("{") == NULL, "open object rejected");
    CHECK(fjson_parse("[1,2") == NULL, "open array rejected");
    CHECK(fjson_parse("\"abc") == NULL, "open string rejected");
    CHECK(fjson_parse("{\"a\":1,}") == NULL, "trailing comma rejected");
    CHECK(fjson_parse("{\"a\":1} {\"b\":2}") == NULL, "trailing garbage rejected");
    CHECK(fjson_parse("\\ud800") == NULL, "lone surrogate rejected");
    CHECK(fjson_parse("5,") == NULL, "trailing comma number garbage");
}

static void test_build_and_write(void)
{
    fj_value *root = fjson_object_new();
    fj_value *log  = fjson_array_new();
    fjson_object_set(root, "name", fjson_string_new("FORGE \"agent\""));
    fjson_object_set(root, "pid", fjson_number_new(123));
    fjson_object_set(root, "on", fjson_bool_new(1));
    fjson_object_set(root, "nil", fjson_null_new());

    fj_value *e = fjson_object_new();
    fjson_object_set(e, "step", fjson_number_new(1));
    fjson_object_set(e, "state", fjson_string_new("WAITING\tTOOL"));
    fjson_array_push(log, e);
    fjson_object_set(root, "log", log);

    fbuf out; fb_init(&out);
    CHECK(fjson_write(root, &out) == 0, "write ok");
    CHECK(out.data != NULL && strcmp(out.data,
        "{\"name\":\"FORGE \\\"agent\\\"\",\"pid\":123,\"on\":true,\"nil\":null,"
        "\"log\":[{\"step\":1,\"state\":\"WAITING\\tTOOL\"}]}") == 0,
        "writer output exact");
    fb_free(&out);
    fjson_free(root);
}

static void test_roundtrip(void)
{
    const char *src = "{\"a\":[1,2,{\"k\":\"v\"}],\"b\":true}";
    fj_value *v = fjson_parse(src);
    CHECK(v != NULL, "parse");
    fbuf out; fb_init(&out);
    fjson_write(v, &out);
    fj_value *v2 = fjson_parse(out.data);
    CHECK(v2 != NULL, "write -> parse round trip");
    CHECK(v2 && fjson_num(fjson_at(fjson_get(v, "a"), 1)) ==
                fjson_num(fjson_at(fjson_get(v2, "a"), 1)), "values survive");
    fb_free(&out);
    fjson_free(v);
    fjson_free(v2);
}

int main(void)
{
    test_parse_config_like();
    test_escapes();
    test_build_and_write();
    test_roundtrip();
    if (g_fail == 0) {
        printf("OK test_forge_json: all tests passed\n");
        return 0;
    }
    printf("FAILED test_forge_json: %d assertion(s)\n", g_fail);
    return 1;
}
