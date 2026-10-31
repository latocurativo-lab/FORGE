/* state_machine.c -- see state_machine.h.
 *
 * The rule table below is the single source of truth: sm_step() linearly
 * matches it by (state, event). It is O(n) with n = 22 rules; for an agent
 * loop that ticks once per model turn, that is far below any I/O cost, and
 * keeping the table exhaustive and readable beats a hash.
 */
#include "state_machine.h"

#include "forge_json.h"
#include "forge_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { sm_state from; sm_event ev; sm_state to; } sm_rule;

static const sm_rule RULES[] = {
    /* normal cycle (happy path mandated by M2: PLANNING -> EXECUTING ->
     * WAITING_TOOL -> VERIFYING -> COMPLETED) */
    { ST_IDLE,        EV_TASK_START,        ST_PLANNING     },
    { ST_PLANNING,    EV_PLAN_READY,        ST_EXECUTING    },
    { ST_EXECUTING,   EV_TOOL_REQUESTED,    ST_WAITING_TOOL },
    { ST_WAITING_TOOL,EV_TOOL_RESULT_OK,    ST_VERIFYING    },
    { ST_VERIFYING,   EV_VERIFY_OK,         ST_COMPLETED    },
    { ST_VERIFYING,   EV_CHAT_DONE,         ST_COMPLETED    },
    { ST_VERIFYING,   EV_VERIFY_UNKNOWN,    ST_BLOCKED      },
    { ST_VERIFYING,   EV_MORE_WORK,         ST_EXECUTING    }, /* next step */

    /* failure handling */
    { ST_WAITING_TOOL, EV_TOOL_RESULT_BAD,  ST_RECOVERING   },
    { ST_VERIFYING,    EV_VERIFY_FAIL,      ST_RECOVERING   },
    { ST_RECOVERING,   EV_RECOVERED,        ST_EXECUTING    },
    { ST_RECOVERING,   EV_RECOVERY_FAILED,  ST_BLOCKED      },

    /* FAIL from every active state */
    { ST_IDLE,        EV_FAIL, ST_FAILED },
    { ST_PLANNING,    EV_FAIL, ST_FAILED },
    { ST_EXECUTING,   EV_FAIL, ST_FAILED },
    { ST_WAITING_TOOL,EV_FAIL, ST_FAILED },
    { ST_VERIFYING,   EV_FAIL, ST_FAILED },
    { ST_RECOVERING,  EV_FAIL, ST_FAILED },

    /* CANCEL from every non-terminal state */
    { ST_IDLE,        EV_CANCEL, ST_CANCELLED },
    { ST_PLANNING,    EV_CANCEL, ST_CANCELLED },
    { ST_EXECUTING,   EV_CANCEL, ST_CANCELLED },
    { ST_WAITING_TOOL,EV_CANCEL, ST_CANCELLED },
    { ST_VERIFYING,   EV_CANCEL, ST_CANCELLED },
    { ST_RECOVERING,  EV_CANCEL, ST_CANCELLED },
};
#define N_RULES (int)(sizeof(RULES) / sizeof(RULES[0]))

const char *sm_state_name(sm_state s)
{
    switch (s) {
    case ST_IDLE:         return "IDLE";
    case ST_PLANNING:     return "PLANNING";
    case ST_EXECUTING:    return "EXECUTING";
    case ST_WAITING_TOOL: return "WAITING_TOOL";
    case ST_VERIFYING:    return "VERIFYING";
    case ST_RECOVERING:   return "RECOVERING";
    case ST_COMPLETED:    return "COMPLETED";
    case ST_BLOCKED:      return "BLOCKED";
    case ST_FAILED:       return "FAILED";
    case ST_CANCELLED:    return "CANCELLED";
    }
    return "?";
}

const char *sm_event_name(sm_event e)
{
    switch (e) {
    case EV_TASK_START:       return "TASK_START";
    case EV_PLAN_READY:       return "PLAN_READY";
    case EV_TOOL_REQUESTED:   return "TOOL_REQUESTED";
    case EV_TOOL_RESULT_OK:   return "TOOL_RESULT_OK";
    case EV_TOOL_RESULT_BAD:  return "TOOL_RESULT_BAD";
    case EV_VERIFY_OK:        return "VERIFY_OK";
    case EV_MORE_WORK:        return "MORE_WORK";
    case EV_VERIFY_FAIL:      return "VERIFY_FAIL";
    case EV_RECOVERED:        return "RECOVERED";
    case EV_RECOVERY_FAILED:  return "RECOVERY_FAILED";
    case EV_FAIL:             return "FAIL";
    case EV_CANCEL:           return "CANCEL";
    case EV_CHAT_DONE:        return "CHAT_DONE";
    case EV_VERIFY_UNKNOWN:   return "VERIFY_UNKNOWN";
    }
    return "?";
}

int sm_state_terminal(sm_state s)
{
    return s == ST_COMPLETED || s == ST_BLOCKED ||
           s == ST_FAILED    || s == ST_CANCELLED;
}

void sm_init(sm_machine *m)
{
    memset(m, 0, sizeof(*m));
    m->state = ST_IDLE;
}

void sm_free(sm_machine *m)
{
    for (size_t i = 0; i < m->n; i++) free(m->log[i].note);
    free(m->log);
    memset(m, 0, sizeof(*m));
}

static int log_entry(sm_machine *m, sm_event ev, sm_state to, int accepted, const char *note)
{
    if (m->n == m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 32;
        sm_entry *p = realloc(m->log, nc * sizeof(sm_entry));
        if (!p) return -1;
        m->log = p;
        m->cap = nc;
    }
    sm_entry *e = &m->log[m->n++];
    e->seq = (long)m->n;
    e->from = m->state;
    e->ev = ev;
    e->to = to;
    e->accepted = accepted;
    e->note = fstrdup(note ? note : "");
    return 0;
}

int sm_step(sm_machine *m, sm_event ev)
{
    for (int i = 0; i < N_RULES; i++) {
        if (RULES[i].from == m->state && RULES[i].ev == ev) {
            if (log_entry(m, ev, RULES[i].to, 1, "") == 0) {
                m->state = RULES[i].to;
                return (int)m->state;
            }
            log_entry(m, ev, m->state, 0, "audit OOM, transition refused");
            return -1;
        }
    }
    char nbuf[192];
    snprintf(nbuf, sizeof(nbuf), "illegal transition %s --%s--> (refused)",
             sm_state_name(m->state), sm_event_name(ev));
    log_entry(m, ev, m->state, 0, nbuf);
    return -1;
}

const sm_entry *sm_log(const sm_machine *m)
{
    return m->log;
}

size_t sm_log_count(const sm_machine *m)
{
    return m->n;
}

int sm_dump_json(const sm_machine *m, const char *path)
{
    fj_value *root = fjson_object_new();
    fj_value *mach = fjson_object_new();
    fj_value *logv = fjson_array_new();
    if (!root || !mach || !logv) goto oom;

    if (fjson_object_set(mach, "state", fjson_string_new(sm_state_name(m->state))) != 0) goto oom;
    if (fjson_object_set(root, "machine", mach) != 0) goto oom;
    mach = NULL; /* owned by root */

    for (size_t i = 0; i < m->n; i++) {
        const sm_entry *e = &m->log[i];
        fj_value *je = fjson_object_new();
        if (!je) goto oom;
        int bad =
            fjson_object_set(je, "seq",      fjson_number_new((double)e->seq))                          ||
            fjson_object_set(je, "from",     fjson_string_new(sm_state_name(e->from)))                  ||
            fjson_object_set(je, "event",    fjson_string_new(sm_event_name(e->ev)))                    ||
            fjson_object_set(je, "to",       fjson_string_new(sm_state_name(e->to)))                    ||
            fjson_object_set(je, "accepted", fjson_bool_new(e->accepted))                               ||
            fjson_object_set(je, "note",     fjson_string_new(e->note ? e->note : ""));
        if (bad != 0) { fjson_free(je); goto oom; }
        if (fjson_array_push(logv, je) != 0) { fjson_free(je); goto oom; }
    }
    if (fjson_object_set(root, "log", logv) != 0) goto oom;
    logv = NULL;

    fbuf out; fb_init(&out);
    if (fjson_write(root, &out) != 0) { fb_free(&out); goto oom; }
    FILE *f = fopen(path, "wb");
    if (!f) { fb_free(&out); fjson_free(root); return -1; }
    size_t w = fwrite(out.data, 1, out.len, f);
    int ok = (w == out.len);
    fclose(f);
    fb_free(&out);
    fjson_free(root);
    return ok ? 0 : -1;

oom:
    fjson_free(root);
    fjson_free(mach);
    fjson_free(logv);
    return -1;
}
