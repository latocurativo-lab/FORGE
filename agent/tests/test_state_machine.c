/* test_state_machine.c -- unit tests for the FORGE agent state machine.
 *
 * Mandated happy loop:
 *   IDLE -> PLANNING -> EXECUTING -> WAITING_TOOL -> VERIFYING -> COMPLETED
 * Plus: illegal transitions are refused, audit log is complete and correct,
 * recovery path and JSON dump are exercised.
 */
#include "../src/state_machine.h"
#include "../../shared/src/forge_json.h"

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

static void step_ok(sm_machine *m, sm_event e, sm_state want, const char *ctx)
{
    sm_state s = sm_step(m, e);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: stepped to %s", ctx, sm_state_name(s));
    CHECK(s == want, buf);
}

/* --- happy path exactly as mandated -------------------------------------- */
static void test_happy_path(void)
{
    sm_machine m;
    sm_init(&m);
    step_ok(&m, EV_TASK_START,      ST_PLANNING,     "boot");
    step_ok(&m, EV_PLAN_READY,      ST_EXECUTING,    "plan ok");
    step_ok(&m, EV_TOOL_REQUESTED,  ST_WAITING_TOOL, "model calls a tool");
    step_ok(&m, EV_TOOL_RESULT_OK,  ST_VERIFYING,    "tool returned");
    step_ok(&m, EV_VERIFY_OK,       ST_COMPLETED,    "verified");
    CHECK(sm_state_terminal(ST_COMPLETED), "COMPLETED is terminal");
    CHECK(m.state == ST_COMPLETED, "ends in COMPLETED");

    /* audit log: 5 entries, one per accepted transition, in order */
    CHECK(sm_log_count(&m) == 5, "audit log has 5 entries");
    const sm_entry *L = sm_log(&m);
    CHECK(L && L[0].from == ST_IDLE && L[0].to == ST_PLANNING && L[0].ev == EV_TASK_START, "entry 1");
    CHECK(L && L[1].from == ST_PLANNING && L[1].to == ST_EXECUTING && L[1].ev == EV_PLAN_READY, "entry 2");
    CHECK(L && L[2].from == ST_EXECUTING && L[2].to == ST_WAITING_TOOL && L[2].ev == EV_TOOL_REQUESTED, "entry 3");
    CHECK(L && L[3].from == ST_WAITING_TOOL && L[3].to == ST_VERIFYING && L[3].ev == EV_TOOL_RESULT_OK, "entry 4");
    CHECK(L && L[4].from == ST_VERIFYING && L[4].to == ST_COMPLETED && L[4].ev == EV_VERIFY_OK, "entry 5");

    long seq_expect = 1;
    for (size_t i = 0; i < sm_log_count(&m); i++) {
        CHECK(L[i].seq == seq_expect++, "seq strictly increments");
        CHECK(L[i].accepted == 1, "all happy-path entries accepted");
    }
    sm_free(&m);
}

static void test_conversation_completion_without_verifier(void)
{
    sm_machine m;
    sm_init(&m);
    sm_step(&m, EV_TASK_START);
    sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED);
    sm_step(&m, EV_TOOL_RESULT_OK);
    step_ok(&m, EV_CHAT_DONE, ST_COMPLETED, "conversation ends without verifier");
    CHECK(sm_state_terminal(m.state), "chat is terminal");
    sm_free(&m);
}

static void test_verification_unknown_is_not_completed(void)
{
    sm_machine m; sm_init(&m); sm_step(&m, EV_TASK_START); sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED); sm_step(&m, EV_TOOL_RESULT_OK);
    step_ok(&m, EV_VERIFY_UNKNOWN, ST_BLOCKED, "missing verification evidence");
    CHECK(sm_state_terminal(m.state), "unknown verification is terminal");
    CHECK(m.state != ST_COMPLETED, "unknown verification is not completed");
    sm_free(&m);
}

/* --- illegal transitions are refused and logged --------------------------- */
static void test_illegal_transitions(void)
{
    sm_machine m;
    sm_init(&m);

    CHECK(sm_step(&m, EV_TOOL_REQUESTED) < 0, "IDLE cannot EXECUTE-WAIT directly");
    CHECK(m.state == ST_IDLE, "state unchanged after refusal");

    CHECK(sm_step(&m, EV_VERIFY_OK) < 0, "IDLE cannot VERIFY");
    CHECK(sm_step(&m, EV_RECOVERED) < 0, "IDLE cannot RECOVER");
    CHECK(m.state == ST_IDLE, "still IDLE");

    CHECK(sm_log_count(&m) == 3, "3 refusals logged");
    const sm_entry *L = sm_log(&m);
    for (size_t i = 0; i < sm_log_count(&m); i++) {
        CHECK(L[i].accepted == 0, "refusal recorded as accepted=0");
        CHECK(L[i].to == L[i].from, "refused entry keeps state");
        CHECK(L[i].note && strlen(L[i].note) > 0, "refusal has a reason");
    }

    /* when terminal: no more transitions allowed at all */
    sm_machine t;
    sm_init(&t);
    sm_step(&t, EV_TASK_START);
    sm_step(&t, EV_PLAN_READY);
    sm_step(&t, EV_TOOL_REQUESTED);
    sm_step(&t, EV_TOOL_RESULT_OK);
    sm_step(&t, EV_VERIFY_OK);   /* -> COMPLETED */
    CHECK(t.state == ST_COMPLETED, "reached COMPLETED");
    CHECK(sm_step(&t, EV_MORE_WORK) < 0, "COMPLETED is FINAL (no more work)");
    CHECK(sm_step(&t, EV_FAIL) < 0, "COMPLETED cannot FAIL");
    CHECK(sm_step(&t, EV_CANCEL) < 0, "COMPLETED cannot be CANCELLED");
    CHECK(t.state == ST_COMPLETED, "state still COMPLETED");
    sm_free(&t);
    sm_free(&m);
}

/* --- recovery path: FAILURE -> RECOVERING -> back, or BLOCKED -------------- */
static void test_recovery(void)
{
    sm_machine m;
    sm_init(&m);
    sm_step(&m, EV_TASK_START);
    sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED);
    step_ok(&m, EV_TOOL_RESULT_BAD, ST_RECOVERING, "tool result unparsable");
    step_ok(&m, EV_RECOVERED,      ST_EXECUTING,   "recovered");
    sm_step(&m, EV_TOOL_REQUESTED);
    sm_step(&m, EV_TOOL_RESULT_BAD);
    step_ok(&m, EV_RECOVERY_FAILED, ST_BLOCKED, "recovery budget exhausted");
    CHECK(sm_state_terminal(ST_BLOCKED), "BLOCKED is terminal");
    sm_free(&m);
}

/* --- cancel path from mid-cycle ------------------------------------------- */
static void test_cancel(void)
{
    sm_machine m;
    sm_init(&m);
    sm_step(&m, EV_TASK_START);
    sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED);
    step_ok(&m, EV_CANCEL, ST_CANCELLED, "user cancels while tool running");
    CHECK(sm_state_terminal(ST_CANCELLED), "CANCELLED is terminal");
    CHECK(sm_step(&m, EV_PLAN_READY) < 0, "CANCELLED is final");
    sm_free(&m);
}

/* --- failure and FAIL from a mid-cycle state ------------------------------ */
static void test_fail_from_verify(void)
{
    sm_machine m;
    sm_init(&m);
    sm_step(&m, EV_TASK_START);
    sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED);
    sm_step(&m, EV_TOOL_RESULT_OK);
    step_ok(&m, EV_FAIL, ST_FAILED, "verification blew up");
    CHECK(sm_state_terminal(ST_FAILED), "FAILED is terminal");
    sm_free(&m);
}

/* --- audit log dump to valid JSON ----------------------------------------- */
static void test_dump_json(void)
{
    const char *path = "state_machine_dump_test.json";
    sm_machine m;
    sm_init(&m);
    sm_step(&m, EV_TASK_START);
    sm_step(&m, EV_PLAN_READY);
    sm_step(&m, EV_TOOL_REQUESTED);   /* accepted */
    CHECK(sm_step(&m, EV_VERIFY_OK) < 0, "VERIFY_OK illegal from WAITING_TOOL");
    CHECK(m.state == ST_WAITING_TOOL, "refusal left state intact");
    CHECK(sm_dump_json(&m, path) == 0, "dump wrote the file");

    /* round-trip: what we wrote must parse back with our own JSON module */
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "dump file exists");
    if (!f) { sm_free(&m); return; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    CHECK(text && fread(text, 1, (size_t)len, f) == (size_t)len, "read dump");
    if (text) text[len] = '\0';
    fclose(f);

    fj_value *v = fjson_parse(text);
    CHECK(v != NULL, "dump parses back as JSON");
    const fj_value *logv = v ? fjson_get(v, "log") : NULL;
    CHECK(logv && fjson_count(logv) == 4, "log array has 4 entries");
    const fj_value *e1 = logv ? fjson_at(logv, 0) : NULL;
    CHECK(e1 && strcmp(fjson_str(fjson_get(e1, "from")), "IDLE") == 0, "entry 1 from");
    CHECK(e1 && strcmp(fjson_str(fjson_get(e1, "to")), "PLANNING") == 0, "entry 1 to");
    CHECK(e1 && fjson_num(fjson_get(e1, "seq")) == 1, "entry 1 seq");
    const fj_value *e3 = logv ? fjson_at(logv, 2) : NULL;
    CHECK(e3 && strcmp(fjson_str(fjson_get(e3, "event")), "TOOL_REQUESTED") == 0, "entry 3 event");
    CHECK(e3 && fjson_bool(fjson_get(e3, "accepted")) == 1, "entry 3 accepted");
    const fj_value *e4 = logv ? fjson_at(logv, 3) : NULL;
    CHECK(e4 && fjson_bool(fjson_get(e4, "accepted")) == 0, "entry 4 (refusal) accepted=0");
    CHECK(v && strcmp(fjson_str(fjson_get(fjson_get(v, "machine"), "state")), "WAITING_TOOL") == 0,
          "machine state dumped");
    fjson_free(v);
    free(text);
    remove(path);
    sm_free(&m);
}

int main(void)
{
    test_happy_path();
    test_conversation_completion_without_verifier();
    test_verification_unknown_is_not_completed();
    test_illegal_transitions();
    test_recovery();
    test_cancel();
    test_fail_from_verify();
    test_dump_json();
    if (g_fail == 0) {
        printf("OK test_state_machine: all tests passed\n");
        return 0;
    }
    printf("FAILED test_state_machine: %d assertion(s)\n", g_fail);
    return 1;
}
