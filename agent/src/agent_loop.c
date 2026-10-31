#include "agent_loop.h"
#include "forge_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#ifdef ST_BLOCKED
#undef ST_BLOCKED
#endif
#endif

void ag_init(ag_session *s) {
    memset(s, 0, sizeof(*s)); sm_init(&s->machine); fb_init(&s->transcript);
    s->max_turns = 100; s->max_failures = 3; s->max_repeats = 3;
}
void ag_free(ag_session *s) {
    sm_free(&s->machine); fb_free(&s->transcript); free(s->last_call);
    memset(s, 0, sizeof(*s));
}

static int add(ag_session *s, const char *label, const char *text) {
    return fb_append(&s->transcript, label) || fb_append(&s->transcript, text) ||
           fb_append(&s->transcript, "\n");
}

int ag_checkpoint_save(const ag_session *s, const char *path, int safe) {
    fj_value *v; fbuf b; FILE *f; int ok;
#ifdef _WIN32
    char full[MAX_PATH], directory[MAX_PATH], temp[MAX_PATH];
    char *leaf;
    DWORD len;
#endif
    if (!path) return 0;
    v = fjson_object_new(); if (!v) return -1;
    if (fjson_object_set(v, "version", fjson_number_new(1)) ||
        fjson_object_set(v, "safe", fjson_bool_new(safe)) ||
        fjson_object_set(v, "state", fjson_number_new(s->machine.state)) ||
        fjson_object_set(v, "turns", fjson_number_new(s->turns)) ||
        fjson_object_set(v, "failures", fjson_number_new(s->failures)) ||
        fjson_object_set(v, "repeats", fjson_number_new(s->repeats)) ||
        fjson_object_set(v, "last_call", fjson_string_new(s->last_call ? s->last_call : "")) ||
        fjson_object_set(v, "transcript", fjson_string_new(s->transcript.data ? s->transcript.data : ""))) {
        fjson_free(v); return -1;
    }
    fb_init(&b); if (fjson_write(v, &b)) { fjson_free(v); fb_free(&b); return -1; }
    /* Create the temporary file in the destination directory so the final
     * replacement cannot cross volumes. Never truncate the last checkpoint. */
#ifdef _WIN32
    len = GetFullPathNameA(path, MAX_PATH, full, &leaf);
    if (!len || len >= MAX_PATH || !leaf || leaf == full) {
        fjson_free(v); fb_free(&b); return -1;
    }
    memcpy(directory, full, (size_t)(leaf - full));
    directory[leaf - full] = '\0';
    if (!GetTempFileNameA(directory, "fgc", 0, temp)) {
        fprintf(stderr, "checkpoint temporary file failed (Windows error %lu)\n", (unsigned long)GetLastError());
        fjson_free(v); fb_free(&b); return -1;
    }
    f = fopen(temp, "wb");
    if (!f) { DeleteFileA(temp); fjson_free(v); fb_free(&b); return -1; }
    ok = fwrite(b.data, 1, b.len, f) == b.len;
    if (fflush(f) || _commit(_fileno(f))) { perror("checkpoint flush"); ok = 0; }
    if (fclose(f)) ok = 0;
    if (ok && !MoveFileExA(temp, full, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "checkpoint replacement failed (Windows error %lu)\n", (unsigned long)GetLastError());
        ok = 0;
    }
    if (!ok) DeleteFileA(temp);
#else
    /* No atomicity guarantee on other platforms yet: fail closed instead of
     * silently overwriting a recoverable checkpoint. */
    (void)f; (void)ok;
    fjson_free(v); fb_free(&b); return -1;
#endif
    fjson_free(v); fb_free(&b); return ok ? 0 : -1;
}

int ag_checkpoint_load(ag_session *s, const char *path) {
    FILE *f; long n; char *buf; fj_value *v; const fj_value *x; int state; unsigned turns, failures, repeats;
    const char *transcript, *last;
    f = fopen(path, "rb"); if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || n > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    buf = malloc((size_t)n + 1); if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return -1; }
    fclose(f); buf[n] = 0; v = fjson_parse(buf); free(buf); if (!v) return -1;
    x = fjson_get(v, "version"); if (!x || x->type != FJ_NUMBER || x->num != 1) goto bad;
    x = fjson_get(v, "safe"); if (!x || x->type != FJ_BOOL || !x->bval) goto bad;
    x = fjson_get(v, "state"); if (!x || x->type != FJ_NUMBER || x->num != ST_EXECUTING) goto bad;
    state = (int)x->num;
#define READ_COUNT(name, dest) do { x = fjson_get(v, name); if (!x || x->type != FJ_NUMBER || x->num < 0 || x->num > 1000000 || x->num != (unsigned)x->num) goto bad; dest = (unsigned)x->num; } while (0)
    READ_COUNT("turns", turns); READ_COUNT("failures", failures); READ_COUNT("repeats", repeats);
#undef READ_COUNT
    x = fjson_get(v, "transcript"); if (!x || x->type != FJ_STRING) goto bad; transcript = x->str;
    x = fjson_get(v, "last_call"); if (!x || x->type != FJ_STRING) goto bad; last = x->str;
    if (s->machine.state != ST_IDLE || s->transcript.len) goto bad;
    if (fb_append(&s->transcript, transcript)) goto bad;
    s->last_call = fstrdup(last); if (!s->last_call) { fb_free(&s->transcript); fb_init(&s->transcript); goto bad; }
    s->machine.state = (sm_state)state; s->turns = turns; s->failures = failures; s->repeats = repeats;
    fjson_free(v); return 0;
bad: fjson_free(v); return -1;
}

static int step(ag_session *s, sm_event e) { return sm_step(&s->machine, e) < 0 ? -1 : 0; }
static int recover(ag_session *s) {
    s->failures++;
    return step(s, s->failures >= s->max_failures ? EV_RECOVERY_FAILED : EV_RECOVERED);
}

int ag_run(ag_session *s, const char *task, const ag_adapter *a, const char *checkpoint) {
    if (!s || !a || !a->generate || !a->tool || !a->verify || !s->max_turns || !s->max_failures || !s->max_repeats) return -1;
    if (s->machine.state == ST_IDLE) {
        if (!task || add(s, "USER: ", task) || step(s, EV_TASK_START) || step(s, EV_PLAN_READY)) return -1;
    }
    if (s->machine.state != ST_EXECUTING) return -1;
    while (s->machine.state == ST_EXECUTING) {
        ag_reply r = {NULL, AG_RUNTIME_ERROR}; tc_result parsed; int bad = 0;
        if (s->turns >= s->max_turns) { if (step(s, EV_FAIL)) return -1; break; }
        if (ag_checkpoint_save(s, checkpoint, 1)) return -1;
        s->turns++;
        if (a->generate(a->ctx, s->transcript.data, &r) || !r.text || r.stop == AG_RUNTIME_ERROR) { if (step(s, EV_FAIL)) return -1; break; }
        if (add(s, "ASSISTANT: ", r.text)) return -1;
        tc_parse(r.text, &parsed);
        if (step(s, EV_TOOL_REQUESTED)) { tc_result_free(&parsed); return -1; }
        if (r.stop == AG_TRUNCATED || (r.stop == AG_TOOL_CALL && (parsed.n != 1 || parsed.n_issues || !parsed.calls[0].ok)) ||
            (r.stop == AG_TURN_END && (parsed.n || parsed.n_issues)) || (r.stop != AG_TOOL_CALL && r.stop != AG_TURN_END)) bad = 1;
        if (!bad && r.stop == AG_TOOL_CALL) {
            const char *result = NULL; char *key = fstrdup(r.text);
            if (!key) { tc_result_free(&parsed); return -1; }
            s->repeats = s->last_call && strcmp(s->last_call, key) == 0 ? s->repeats + 1 : 1;
            free(s->last_call); s->last_call = key;
            if (s->repeats > s->max_repeats) bad = 1;
            else {
                /* Persist uncertainty before any possibly non-idempotent action. */
                if (ag_checkpoint_save(s, checkpoint, 0)) { tc_result_free(&parsed); return -1; }
                if (a->tool(a->ctx, &parsed.calls[0], &result) || !result) {
                    /* Outcome is unknown: do not retry or mark checkpoint safe. */
                    tc_result_free(&parsed);
                    if (step(s, EV_TOOL_RESULT_BAD) || step(s, EV_RECOVERY_FAILED)) return -1;
                    return (int)s->machine.state;
                }
                else if (add(s, "TOOL: ", result)) { tc_result_free(&parsed); return -1; }
            }
        }
        tc_result_free(&parsed);
        if (bad) {
            if (add(s, "SYSTEM: ", "Invalid, truncated, repeated or failed operation; revise the next response.") || step(s, EV_TOOL_RESULT_BAD) || recover(s)) return -1;
            continue;
        }
        if (step(s, EV_TOOL_RESULT_OK)) return -1;
        if (r.stop == AG_TOOL_CALL) {
            if (step(s, EV_MORE_WORK)) return -1;
        } else {
            ag_verdict v = a->verify(a->ctx, s->transcript.data, r.text);
            if (v == AG_VERIFY_DONE) { if (step(s, EV_VERIFY_OK)) return -1; }
            else if (v == AG_VERIFY_MORE) { if (add(s, "SYSTEM: ", "Verification requires more work.") || step(s, EV_MORE_WORK)) return -1; }
            else if (v == AG_VERIFY_RETRY) { if (add(s, "SYSTEM: ", "Verification failed; revise your answer.") || step(s, EV_VERIFY_FAIL) || recover(s)) return -1; }
            else { if (step(s, EV_FAIL)) return -1; }
        }
    }
    if (ag_checkpoint_save(s, checkpoint, 0)) return -1;
    return (int)s->machine.state;
}
