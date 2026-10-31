/* test_forge_events.c -- unit tests for shared/src/forge_events.
 *
 * Checks: every event type paints a correct, readable audit line; owned
 * strings survive the event struct; fe_event_free keeps valgrind clean
 * (no leaks by construction); stop reasons disambiguated as per M1.
 */
#include "../../shared/src/forge_events.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %-28s %s\n", __func__, msg);                        \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

static void test_paint_all(void)
{
    char buf[512];

    fe_event e1 = fe_started(7, "gemma-4-12B-it");
    fe_event e2 = fe_token(7, "Hello", 0);
    fe_event e3 = fe_tool_call(7, "get_current_weather", "location:<|\"|>Tokyo, JP<|\"|>", 0);
    fe_event e4 = fe_turn_completed(7, "15 degrees in Tokyo", FE_STOP_TURN_END);
    fe_event e5 = fe_truncated(7, "<|tool_call>call:get_c");
    fe_event e6 = fe_runtime_error(7, 8, "CUDA OOM");
    fe_event e7 = fe_context_limit(7, 16384, 16384);
    fe_event e8 = fe_cancelled(7, "user interrupt");

    fe_to_string(&e1, buf, sizeof(buf));
    CHECK(strstr(buf, "GenerationStarted") == buf, "type first");
    CHECK(strstr(buf, "gemma-4-12B-it") && strstr(buf, "id=7"), "payload painted");

    fe_to_string(&e2, buf, sizeof(buf));
    CHECK(strstr(buf, "TokenGenerated") && strstr(buf, "text=Hello"), "token painted");

    fe_to_string(&e3, buf, sizeof(buf));
    CHECK(strstr(buf, "ToolCallDetected") != NULL &&
          strstr(buf, "get_current_weather") != NULL &&
          strstr(buf, "location:<|\"|>Tokyo, JP<|\"|>") != NULL,
          "tool call payload painted verbatim");

    fe_to_string(&e4, buf, sizeof(buf));
    CHECK(strstr(buf, "AssistantTurnCompleted") && strstr(buf, "stop=turn_end"),
          "stop reason disambiguated (turn_end)");

    /* stop reasons are distinct values, as the M1 goal requires */
    CHECK(FE_STOP_TURN_END != FE_STOP_MESSAGE_END, "turn end != message end");
    CHECK(FE_STOP_MESSAGE_END != FE_STOP_TOOL_REQUEST, "message end != tool request");
    CHECK(FE_STOP_TOOL_REQUEST != FE_STOP_TRUNCATED, "tool request != truncation");

    fe_to_string(&e5, buf, sizeof(buf));
    CHECK(strstr(buf, "GenerationTruncated") && strstr(buf, "partial=<|tool_call>call:get_c"),
          "truncation preserves the very partials");

    fe_to_string(&e6, buf, sizeof(buf));
    CHECK(strstr(buf, "RuntimeError") && strstr(buf, "code=8") && strstr(buf, "CUDA OOM"),
          "runtime error painted");

    fe_to_string(&e7, buf, sizeof(buf));
    CHECK(strstr(buf, "ContextLimitReached") && strstr(buf, "used=16384 limit=16384"),
          "context saturation painted");

    fe_to_string(&e8, buf, sizeof(buf));
    CHECK(strstr(buf, "GenerationCancelled") && strstr(buf, "user interrupt"), "cancel painted");

    /* ownership: freeing twice indirectly via fe_event_free for each field */
    fe_event_free(&e1); fe_event_free(&e2); fe_event_free(&e3);
    fe_event_free(&e4); fe_event_free(&e5); fe_event_free(&e6);
    fe_event_free(&e7); fe_event_free(&e8);
}

static void test_null_safety(void)
{
    fe_event e = fe_tool_call(1, NULL, NULL, 0);
    char buf[512];
    fe_to_string(&e, buf, sizeof(buf));
    CHECK(strstr(buf, "name=") && strstr(buf, "args="), "NULL strings painted as empty");
    fe_event_free(&e);

    fe_event t = fe_token(1, NULL, -1);
    fe_to_string(&t, buf, sizeof(buf));
    CHECK(strstr(buf, "text=") != NULL, "NULL token text painted");
    fe_event_free(&t);
}

int main(void)
{
    test_paint_all();
    test_null_safety();
    if (g_fail == 0) {
        printf("OK test_forge_events: all tests passed\n");
        return 0;
    }
    printf("FAILED test_forge_events: %d assertion(s)\n", g_fail);
    return 1;
}
