#include "agent_loop.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#ifdef ST_BLOCKED
#undef ST_BLOCKED
#endif
#endif

typedef struct { int turn, tools, verify, mode; } mock;
static int generate(void *p, const char *history, ag_reply *r) {
    mock *m = p; (void)history; m->turn++;
    if (m->mode == 3) { r->stop = AG_TRUNCATED; r->text = "partial"; }
    else if (m->mode == 4) { r->stop = AG_TOOL_CALL; r->text = "not a tool call"; }
    else if (m->mode == 1 || m->turn == 1) { r->stop = AG_TOOL_CALL; r->text = "<|tool_call>call:read{path:<|\"|>x<|\"|>}<tool_call|>"; }
    else { r->stop = AG_TURN_END; r->text = "done"; }
    return 0;
}
static int tool(void *p, const tc_call *c, const char **out) {
    mock *m = p; (void)c; m->tools++; if (m->mode == 2) return -1; *out = "ok"; return 0;
}
static ag_verdict verify(void *p, const char *h, const char *answer) {
    mock *m = p; (void)h; (void)answer; m->verify++; return AG_VERIFY_DONE;
}
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    ag_session s, loaded; mock m = {0}; ag_adapter a = {generate, tool, verify, &m};
    const char *path = "agent_loop_checkpoint_test.json";
    ag_init(&s); CHECK(ag_run(&s, "task", &a, path) == ST_COMPLETED); CHECK(m.tools == 1 && m.verify == 1);
    ag_init(&loaded); CHECK(ag_checkpoint_load(&loaded, path) != 0); ag_free(&loaded); ag_free(&s);
    memset(&m, 0, sizeof(m)); m.mode = 3; ag_init(&s); s.max_failures = 2;
    { int rc = ag_run(&s, "task", &a, path); if (rc != ST_BLOCKED) printf("truncated run rc=%d state=%d turns=%d\n", rc, s.machine.state, m.turn); CHECK(rc == ST_BLOCKED); } CHECK(m.tools == 0 && m.verify == 0 && m.turn == 2); ag_free(&s);
    memset(&m, 0, sizeof(m)); m.mode = 4; ag_init(&s); s.max_failures = 1;
    CHECK(ag_run(&s, "task", &a, path) == ST_BLOCKED); CHECK(m.tools == 0); ag_free(&s);
    memset(&m, 0, sizeof(m)); m.mode = 1; ag_init(&s); s.max_repeats = 2; s.max_failures = 1;
    CHECK(ag_run(&s, "task", &a, path) == ST_BLOCKED); CHECK(m.tools == 2); ag_free(&s);
    memset(&m, 0, sizeof(m)); m.mode = 2; ag_init(&s);
    CHECK(ag_run(&s, "task", &a, path) == ST_BLOCKED); CHECK(m.tools == 1);
    ag_init(&loaded); CHECK(ag_checkpoint_load(&loaded, path) != 0); ag_free(&loaded); ag_free(&s);
    ag_init(&s); CHECK(sm_step(&s.machine, EV_TASK_START) >= 0); CHECK(sm_step(&s.machine, EV_PLAN_READY) >= 0);
    CHECK(fb_append(&s.transcript, "USER: task\n") == 0); s.turns = 2;
    CHECK(ag_checkpoint_save(&s, path, 1) == 0); ag_init(&loaded);
    CHECK(ag_checkpoint_load(&loaded, path) == 0); CHECK(loaded.turns == 2 && loaded.machine.state == ST_EXECUTING);
    CHECK(strcmp(loaded.transcript.data, s.transcript.data) == 0);
    ag_free(&loaded);
#ifdef _WIN32
    /* A locked destination must leave the previous checkpoint intact. */
    {
        HANDLE lock = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(lock != INVALID_HANDLE_VALUE);
        s.turns = 9;
        CHECK(ag_checkpoint_save(&s, path, 1) != 0);
        CHECK(CloseHandle(lock));
        ag_init(&loaded);
        CHECK(ag_checkpoint_load(&loaded, path) == 0 && loaded.turns == 2);
        ag_free(&loaded);
    }
#endif
    ag_free(&s); remove(path); puts("OK test_agent_loop"); return 0;
}
