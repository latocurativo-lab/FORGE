#include "forge_runtime.h"
#include "forge_json.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr, note) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s: %s\n", __func__, note); \
        failures++; \
    } \
} while (0)

static int decode_fails(const char *json, const char *error_fragment)
{
    fr_reply reply = {0};
    char error[256] = {0};
    int rc = fr_decode_response(json, &reply, error, sizeof(error));
    int ok = rc != 0 && reply.stop == FR_STOP_ERROR && error[0] != '\0' &&
             (!error_fragment || strstr(error, error_fragment) != NULL) &&
             reply.tool_call_count == 0;
    fr_reply_free(&reply);
    return ok;
}

static void test_encode_tool_round_trip(void)
{
    fr_tool_call calls[] = {
        {"call-roundtrip", "echo_safe", "{\"text\":\"PING\"}", 15},
        {"call-second", "add", "{\"a\":2,\"b\":3}", 13}
    };
    fr_message messages[] = {
        {"user", "Call echo_safe and return PONG after the response.", NULL, NULL, 0},
        {"assistant", NULL, NULL, calls, 2},
        {"tool", "PING", "call-roundtrip", NULL, 0},
        {"tool", "5", "call-second", NULL, 0},
        {"user", "Now answer only PONG.", NULL, NULL, 0}
    };
    char *encoded = NULL;
    size_t encoded_len = 0;
    char error[256] = {0};
    CHECK(fr_encode_messages(messages, 5, &encoded, &encoded_len,
                             error, sizeof(error)) == 0, error);
    CHECK(encoded && encoded_len == strlen(encoded), "encoded JSON has explicit length");
    fj_value *tree = encoded ? fjson_parse(encoded) : NULL;
    CHECK(tree && tree->type == FJ_ARRAY && fjson_count(tree) == 5,
          "serialized conversation is a JSON message array");
    if (tree) {
        const fj_value *assistant = fjson_at(tree, 1);
        const fj_value *tool = fjson_at(tree, 2);
        const fj_value *second_tool = fjson_at(tree, 3);
        const fj_value *encoded_call = fjson_at(fjson_get(assistant, "tool_calls"), 0);
        const fj_value *encoded_call2 = fjson_at(fjson_get(assistant, "tool_calls"), 1);
        const fj_value *function = fjson_get(encoded_call, "function");
        const fj_value *function2 = fjson_get(encoded_call2, "function");
        CHECK(strcmp(fjson_str(fjson_get(assistant, "role")), "assistant") == 0,
              "assistant role serialized");
        CHECK(fjson_get(assistant, "content")->type == FJ_NULL,
              "assistant null content retained");
        CHECK(strcmp(fjson_str(fjson_get(encoded_call, "id")), "call-roundtrip") == 0,
              "assistant call ID retained");
        CHECK(strcmp(fjson_str(fjson_get(function, "name")), "echo_safe") == 0,
              "function name retained");
        CHECK(strcmp(fjson_str(fjson_get(function, "arguments")),
                     "{\"text\":\"PING\"}") == 0, "raw arguments JSON retained");
        CHECK(strcmp(fjson_str(fjson_get(encoded_call2, "id")), "call-second") == 0,
              "second assistant call ID retained");
        CHECK(strcmp(fjson_str(fjson_get(function2, "name")), "add") == 0,
              "second function name retained");
        CHECK(strcmp(fjson_str(fjson_get(tool, "role")), "tool") == 0,
              "tool role serialized");
        CHECK(strcmp(fjson_str(fjson_get(tool, "tool_call_id")), "call-roundtrip") == 0,
              "tool response call ID retained");
        CHECK(strcmp(fjson_str(fjson_get(tool, "content")), "PING") == 0,
              "tool response body retained");
        CHECK(strcmp(fjson_str(fjson_get(second_tool, "tool_call_id")), "call-second") == 0,
              "second tool response ID retained");
        CHECK(strcmp(fjson_str(fjson_get(second_tool, "content")), "5") == 0,
              "second tool response retained");
        CHECK(strcmp(fjson_str(fjson_get(fjson_at(tree, 4), "content")),
                     "Now answer only PONG.") == 0, "follow-up message preserved");
    }
    fjson_free(tree);
    free(encoded);

    fr_message mismatch[] = {
        {"assistant", NULL, NULL, calls, 1},
        {"tool", "PING", "wrong-id", NULL, 0}
    };
    encoded = NULL;
    CHECK(fr_encode_messages(mismatch, 2, &encoded, &encoded_len,
                             error, sizeof(error)) != 0 && encoded == NULL,
          "mismatched tool call ID is rejected");

    fr_tool_call malformed = {"call-bad", "echo_safe", "{", 1};
    fr_message bad_args[] = {
        {"assistant", NULL, NULL, &malformed, 1},
        {"tool", "unused", "call-bad", NULL, 0}
    };
    CHECK(fr_encode_messages(bad_args, 2, &encoded, &encoded_len,
                             error, sizeof(error)) != 0 && encoded == NULL,
          "malformed request arguments are rejected");
}

static void test_turn_and_length(void)
{
    fr_reply reply = {0};
    char error[256] = {0};
    const char *json =
        "{\"choices\":[{\"finish_reason\":\"length\",\"message\":{" 
        "\"content\":\"raw \\\"text\\\"\\nline\"}}]}";
    CHECK(fr_decode_response(json, &reply, error, sizeof(error)) == 0, error);
    CHECK(reply.stop == FR_STOP_LENGTH, "length stop mapping");
    CHECK(reply.text && strcmp(reply.text, "raw \"text\"\nline") == 0, "decoded content");
    fr_reply_free(&reply);
}

static void test_multiple_calls_and_null_content(void)
{
    fr_reply reply = {0};
    char error[256] = {0};
    const char *json =
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{" 
        "\"content\":null,\"tool_calls\":["
        "{\"id\":\"call-1\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"echo_safe\",\"arguments\":\"{\\\"text\\\":\\\"PING\\\"}\"}},"
        "{\"id\":\"call-2\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"add\",\"arguments\":\"{\\\"a\\\":2,\\\"b\\\":3}\"}}]}}]}";
    CHECK(fr_decode_response(json, &reply, error, sizeof(error)) == 0, error);
    CHECK(reply.stop == FR_STOP_TOOL_CALL, "tool stop requires valid calls");
    CHECK(reply.text && reply.text[0] == '\0', "null content becomes empty text");
    CHECK(reply.tool_call_count == 2, "preserves all tool calls");
    if (reply.tool_call_count == 2) {
        CHECK(strcmp(reply.tool_calls[0].id, "call-1") == 0, "first id order");
        CHECK(strcmp(reply.tool_calls[0].name, "echo_safe") == 0, "first name");
        CHECK(strcmp(reply.tool_calls[0].arguments, "{\"text\":\"PING\"}") == 0,
              "first arguments retained as JSON text");
        CHECK(reply.tool_calls[0].arguments_len == strlen(reply.tool_calls[0].arguments),
              "first arguments have explicit byte length");
        CHECK(strcmp(reply.tool_calls[1].id, "call-2") == 0, "second id order");
        CHECK(strcmp(reply.tool_calls[1].name, "add") == 0, "second name");
        CHECK(strcmp(reply.tool_calls[1].arguments, "{\"a\":2,\"b\":3}") == 0,
              "second arguments retained");
    }
    fr_reply_free(&reply);
    CHECK(reply.tool_calls == NULL && reply.tool_call_count == 0, "free releases call array");
}

static void test_reject_malformed_or_partial_calls(void)
{
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{" 
        "\"content\":null,\"tool_calls\":[{\"id\":\"x\",\"type\":\"function\","
        "\"function\":{\"name\":\"f\",\"arguments\":\"{\"}}]}}]}",
        "complete JSON"), "invalid arguments JSON rejected");
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{" 
        "\"tool_calls\":[{\"id\":\"x\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"f\"}}]}}]}", "arguments"), "partial function call rejected");
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{" 
        "\"tool_calls\":["
        "{\"id\":\"same\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"f\",\"arguments\":\"{}\"}},"
        "{\"id\":\"same\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"g\",\"arguments\":\"{}\"}}]}}]}", "duplicate"),
        "duplicate IDs rejected");
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"content\":null}}]}",
        "missing or empty"), "tool finish without calls rejected");
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{" 
        "\"tool_calls\":[{\"id\":\"x\",\"type\":\"function\",\"function\":{" 
        "\"name\":\"f\",\"arguments\":\"{}\"}}]}}]}", "not tool_calls"),
        "finish reason mismatch rejected");
    CHECK(decode_fails("{\"choices\":[", "malformed"), "partial response JSON rejected");
    CHECK(decode_fails(
        "{\"choices\":[{\"finish_reason\":\"stop\",\"finish_reason\":\"length\","
        "\"message\":{\"content\":\"x\"}}]}", "duplicate-free"),
        "duplicate response fields rejected");
}

static int live(const char *url)
{
    char error[256] = {0};
    fr_config config = {0};
    config.base_url = url;
    config.timeout_ms = 180000;
    config.max_tokens = 48;
    config.temperature = 0;
    config.thinking = 0;
    fr_reply reply = {0};
    fr_message message = {"user", "Answer with only the integer: 17 * 19 = ?", NULL, NULL, 0};
    if (fr_generate(&config, &message, 1, &reply, error, sizeof(error)) != 0) {
        fprintf(stderr, "live arithmetic failed: %s (HTTP %u)\n", error, reply.http_status);
        fr_reply_free(&reply);
        return 2;
    }
    printf("live arithmetic: stop=%s content=%s\n", reply.finish_reason, reply.text);
    if (strcmp(reply.text, "323") != 0 || reply.stop != FR_STOP_TURN) {
        fprintf(stderr, "unexpected arithmetic response\n");
        fr_reply_free(&reply);
        return 2;
    }
    fr_reply_free(&reply);

    config.max_tokens = 128;
    config.tools_json =
        "[{\"type\":\"function\",\"function\":{\"name\":\"echo_safe\","
        "\"description\":\"Return a harmless supplied string; no side effects\","
        "\"parameters\":{\"type\":\"object\",\"properties\":{" 
        "\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}}}]";
    message.content = "Use echo_safe exactly once with text PING, then answer only PONG after its response.";
    if (fr_generate(&config, &message, 1, &reply, error, sizeof(error)) != 0) {
        fprintf(stderr, "live tool request failed: %s (HTTP %u)\n", error, reply.http_status);
        fr_reply_free(&reply);
        return 3;
    }
    printf("live tool case: finish_reason=%s calls=%zu\n",
           reply.finish_reason, reply.tool_call_count);
    if (reply.stop != FR_STOP_TOOL_CALL || reply.tool_call_count != 1 ||
        strcmp(reply.tool_calls[0].name, "echo_safe") != 0) {
        fprintf(stderr, "expected tool call not returned\n%s\n",
                reply.raw_json ? reply.raw_json : "(none)");
        fr_reply_free(&reply);
        return 3;
    }
    fj_value *args = fjson_parse(reply.tool_calls[0].arguments);
    int safe_call = args && strcmp(fjson_str(fjson_get(args, "text")), "PING") == 0;
    fjson_free(args);
    if (!safe_call) {
        fprintf(stderr, "returned tool arguments are not the fixed PING fixture\n");
        fr_reply_free(&reply);
        return 3;
    }

    fr_message conversation[] = {
        {"user", message.content, NULL, NULL, 0},
        {"assistant", NULL, NULL, reply.tool_calls, reply.tool_call_count},
        {"tool", "PING", reply.tool_calls[0].id, NULL, 0}
    };
    fr_reply followup = {0};
    if (fr_generate(&config, conversation, 3, &followup, error, sizeof(error)) != 0) {
        fprintf(stderr, "live tool follow-up failed: %s (HTTP %u)\n",
                error, followup.http_status);
        fr_reply_free(&followup);
        fr_reply_free(&reply);
        return 4;
    }
    printf("live follow-up: stop=%s content=%s\n", followup.finish_reason, followup.text);
    if (followup.stop != FR_STOP_TURN || strcmp(followup.text, "PONG") != 0) {
        fprintf(stderr, "follow-up did not return the expected PONG\n");
        fr_reply_free(&followup);
        fr_reply_free(&reply);
        return 4;
    }
    puts("live round-trip: structured echo_safe(PING) -> role=tool PING -> PONG; no tool executed");
    fr_reply_free(&followup);
    fr_reply_free(&reply);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--unit") == 0) {
        test_turn_and_length();
        test_multiple_calls_and_null_content();
        test_reject_malformed_or_partial_calls();
        test_encode_tool_round_trip();
        if (!failures) {
            puts("unit: pass (strict decode, multiple calls, request round-trip, tool ID matching)");
            return 0;
        }
        fprintf(stderr, "unit: %d failure(s)\n", failures);
        return 1;
    }
    if (argc == 2) return live(argv[1]);
    fprintf(stderr, "usage: adapter_test --unit | http://127.0.0.1:port\n");
    return 64;
}
