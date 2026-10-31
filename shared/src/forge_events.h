/* forge_events.h -- structured agent/generation events for the FORGE harness.
 *
 * The runtime (llama.cpp fork) and the agent state machine communicate through
 * these events instead of free text, so log/audit tooling can paint them
 * uniformly. C11, libc only. Owned by Agente C.
 */
#ifndef FORGE_EVENTS_H
#define FORGE_EVENTS_H

#include "forge_util.h"
#include <stddef.h>
#include <stdint.h>

/* Stop reasons (M1 goal: fine turno != fine messaggio != richiesta tool). */
typedef enum fe_stop {
    FE_STOP_NONE = 0,          /* stream still open, nothing terminal yet    */
    FE_STOP_TOOL_REQUEST,      /* model emitted one or more tool calls       */
    FE_STOP_TURN_END,          /* '<turn|>' seen: model's turn is over       */
    FE_STOP_MESSAGE_END,       /* eos without '<turn|>': message complete    */
    FE_STOP_TRUNCATED,         /* output cut by max_tokens / length          */
    FE_STOP_CANCELLED          /* stopped by user / system                   */
} fe_stop;

typedef enum fe_type {
    FE_GENERATION_STARTED = 0,
    FE_TOKEN_GENERATED,
    FE_TOOL_CALL_DETECTED,
    FE_ASSISTANT_TURN_COMPLETED,
    FE_GENERATION_TRUNCATED,
    FE_RUNTIME_ERROR,
    FE_CONTEXT_LIMIT_REACHED,
    FE_GENERATION_CANCELLED
} fe_type;

#define FE_MODEL_MAX 48

typedef struct fe_payload {
    union {
        struct { char model[FE_MODEL_MAX];        } started;
        struct { char *text; int index;           } token;
        struct { char *name; char *args; int idx; } tool_call;
        struct { char *text; int stop;            } turn_completed;
        struct { char *partial_text;              } truncated;
        struct { int code; char *message;         } runtime_error;
        struct { int used; int limit;             } context_limit;
        struct { char *reason;                    } cancelled;
    } u;
} fe_payload;

typedef struct fe_event {
    fe_type    type;
    int        id;        /* generation id */
    long long  ts_ms;     /* wall clock ms */
    long       seq;       /* audit sequence, assigned by the event source  */
    fe_payload p;
} fe_event;

const char *fe_type_name(fe_type t);
const char *fe_stop_name(fe_stop s);
long long   fe_now_ms(void);

/* Paint an event into one line for log/audit. Returns buf ("" on truncation
 * risk is avoided: output is always NUL-terminated and cut at cap). */
const char *fe_to_string(const fe_event *e, char *buf, size_t cap);

/* Constructors -- they take copies of the strings, never alias the caller's
 * memory, so the event owns everything it paints. */
fe_event fe_started(int gen, const char *model);
fe_event fe_token(int gen, const char *text, int index);
fe_event fe_tool_call(int gen, const char *name, const char *args, int idx);
fe_event fe_turn_completed(int gen, const char *text, fe_stop stop);
fe_event fe_truncated(int gen, const char *partial);
fe_event fe_runtime_error(int gen, int code, const char *msg);
fe_event fe_context_limit(int gen, int used, int limit);
fe_event fe_cancelled(int gen, const char *reason);

void fe_event_free(fe_event *e);

#endif /* FORGE_EVENTS_H */
