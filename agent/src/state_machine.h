/* state_machine.h -- explicit FORGE agent state machine (style ds4).
 *
 * Ten states, legal transitions enumerated in state_machine.c and validated:
 * anything not in the table is REJECTED (and logged as a rejected entry in
 * the audit log -- rejections are worth knowing in forensics).
 *
 * The machine also keeps an in-memory transition log and can dump it to a
 * JSON file (see sm_dump_json). C11, libc only. Owned by Agente C.
 */
#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stddef.h>

typedef enum sm_state {
    ST_IDLE = 0,
    ST_PLANNING,
    ST_EXECUTING,
    ST_WAITING_TOOL,
    ST_VERIFYING,
    ST_RECOVERING,
    ST_COMPLETED,
    ST_BLOCKED,
    ST_FAILED,
    ST_CANCELLED
} sm_state;

typedef enum sm_event {
    EV_TASK_START = 0,      /* IDLE -> PLANNING                               */
    EV_PLAN_READY,          /* PLANNING -> EXECUTING                          */
    EV_TOOL_REQUESTED,      /* EXECUTING -> WAITING_TOOL                      */
    EV_TOOL_RESULT_OK,      /* WAITING_TOOL -> VERIFYING                      */
    EV_TOOL_RESULT_BAD,     /* WAITING_TOOL -> RECOVERING (bad/unparsable)    */
    EV_VERIFY_OK,           /* VERIFYING -> COMPLETED                         */
    EV_MORE_WORK,           /* VERIFYING -> EXECUTING (next step of the task) */
    EV_VERIFY_FAIL,         /* VERIFYING -> RECOVERING                        */
    EV_RECOVERED,           /* RECOVERING -> EXECUTING                        */
    EV_RECOVERY_FAILED,     /* RECOVERING -> BLOCKED                          */
    EV_FAIL,                /* -> FAILED from active states                   */
    EV_CANCEL,              /* -> CANCELLED from any non-terminal state       */
    EV_CHAT_DONE,           /* VERIFYING -> COMPLETED, conversational only    */
    EV_VERIFY_UNKNOWN       /* VERIFYING -> BLOCKED; no completion evidence   */
} sm_event;

typedef struct sm_entry {
    long      seq;
    sm_state  from;
    sm_event  ev;
    sm_state  to;
    int       accepted;   /* 0 = transition was refused (state unchanged) */
    char     *note;       /* refusal reason when 'accepted'==0, else ""   */
} sm_entry;

typedef struct sm_machine {
    sm_state  state;
    sm_entry *log;
    size_t    n, cap;
} sm_machine;

void         sm_init(sm_machine *m);
void         sm_free(sm_machine *m);

/* Ask event 'ev' of the current state. Returns the new state (>= 0) or
 * -1 if the transition is illegal (state stays put; the refusal is logged). */
int          sm_step(sm_machine *m, sm_event ev);

const char  *sm_state_name(sm_state s);
const char  *sm_event_name(sm_event e);
int          sm_state_terminal(sm_state s);

/* Audit log access. */
const sm_entry *sm_log(const sm_machine *m);
size_t          sm_log_count(const sm_machine *m);

/* Dump the audit log (and final state) as a JSON file. 0 on success.
 * Ex: {"machine":{"state":"COMPLETED"},"log":[{"seq":1,"from":"IDLE",...}]} */
int sm_dump_json(const sm_machine *m, const char *path);

#endif /* STATE_MACHINE_H */
