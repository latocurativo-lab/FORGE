#ifndef FORGE_AGENT_LOOP_H
#define FORGE_AGENT_LOOP_H

#include "state_machine.h"
#include "toolcall_parser.h"

/* Runtime output is owned by the adapter and remains valid until its next call.
 * Never infer completion from text: the runtime must supply the stop reason. */
typedef enum { AG_TURN_END, AG_TOOL_CALL, AG_TRUNCATED, AG_RUNTIME_ERROR } ag_stop;
typedef struct { const char *text; ag_stop stop; } ag_reply;
typedef enum { AG_VERIFY_DONE, AG_VERIFY_MORE, AG_VERIFY_RETRY, AG_VERIFY_ERROR } ag_verdict;
typedef struct {
    int (*generate)(void *ctx, const char *transcript, ag_reply *reply);
    int (*tool)(void *ctx, const tc_call *call, const char **result);
    ag_verdict (*verify)(void *ctx, const char *transcript, const char *answer);
    void *ctx;
} ag_adapter;

typedef struct {
    sm_machine machine;
    fbuf transcript;
    unsigned turns, failures, repeats;
    char *last_call;
    unsigned max_turns, max_failures, max_repeats;
} ag_session;

void ag_init(ag_session *s);
void ag_free(ag_session *s);
/* Task is appended only on a fresh session. Returns terminal state or -1 on I/O/OOM.
 * Checkpoints before side effects are marked unsafe and cannot be resumed. */
int ag_run(ag_session *s, const char *task, const ag_adapter *adapter, const char *checkpoint);
int ag_checkpoint_save(const ag_session *s, const char *path, int safe);
int ag_checkpoint_load(ag_session *s, const char *path);
#endif
