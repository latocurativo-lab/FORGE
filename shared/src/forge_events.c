/* forge_events.c -- see forge_events.h */
#include "forge_events.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

const char *fe_type_name(fe_type t)
{
    switch (t) {
    case FE_GENERATION_STARTED:        return "GenerationStarted";
    case FE_TOKEN_GENERATED:           return "TokenGenerated";
    case FE_TOOL_CALL_DETECTED:        return "ToolCallDetected";
    case FE_ASSISTANT_TURN_COMPLETED:  return "AssistantTurnCompleted";
    case FE_GENERATION_TRUNCATED:      return "GenerationTruncated";
    case FE_RUNTIME_ERROR:             return "RuntimeError";
    case FE_CONTEXT_LIMIT_REACHED:     return "ContextLimitReached";
    case FE_GENERATION_CANCELLED:      return "GenerationCancelled";
    }
    return "?";
}

const char *fe_stop_name(fe_stop s)
{
    switch (s) {
    case FE_STOP_NONE:          return "none";
    case FE_STOP_TOOL_REQUEST:  return "tool_request";
    case FE_STOP_TURN_END:      return "turn_end";
    case FE_STOP_MESSAGE_END:   return "message_end";
    case FE_STOP_TRUNCATED:     return "truncated";
    case FE_STOP_CANCELLED:     return "cancelled";
    }
    return "?";
}

long long fe_now_ms(void)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    /* 100ns units since 1601-01-01 -> ms since 1970-01-01 */
    long long v = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (v - 116444736000000000LL) / 10000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* Append a C-string onto a bounded paint buffer. */
static void paint(char *buf, size_t cap, size_t *n, const char *s)
{
    if (!s) s = "";
    size_t need = strlen(s);
    size_t room = cap > *n + 1 ? cap - *n - 1 : 0;
    if (need > room) need = room;
    memcpy(buf + *n, s, need);
    *n += need;
    buf[*n] = '\0';
}

static void paintf(char *buf, size_t cap, size_t *n, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    paint(buf, cap, n, tmp);
}

const char *fe_to_string(const fe_event *e, char *buf, size_t cap)
{
    if (!buf || cap == 0) return buf;
    size_t n = 0;
    buf[0] = '\0';
    paintf(buf, cap, &n, "%s seq=%ld id=%d ts=%lld", fe_type_name(e->type), e->seq, e->id, e->ts_ms);
    switch (e->type) {
    case FE_GENERATION_STARTED:
        paintf(buf, cap, &n, " model=%s", e->p.u.started.model);
        break;
    case FE_TOKEN_GENERATED:
        paintf(buf, cap, &n, " idx=%d text=%s", e->p.u.token.index, e->p.u.token.text);
        break;
    case FE_TOOL_CALL_DETECTED:
        paintf(buf, cap, &n, " idx=%d name=%s args=%s",
               e->p.u.tool_call.idx, e->p.u.tool_call.name, e->p.u.tool_call.args);
        break;
    case FE_ASSISTANT_TURN_COMPLETED:
        paintf(buf, cap, &n, " stop=%s text=%s",
               fe_stop_name((fe_stop)e->p.u.turn_completed.stop), e->p.u.turn_completed.text);
        break;
    case FE_GENERATION_TRUNCATED:
        paintf(buf, cap, &n, " partial=%s", e->p.u.truncated.partial_text);
        break;
    case FE_RUNTIME_ERROR:
        paintf(buf, cap, &n, " code=%d msg=%s", e->p.u.runtime_error.code, e->p.u.runtime_error.message);
        break;
    case FE_CONTEXT_LIMIT_REACHED:
        paintf(buf, cap, &n, " used=%d limit=%d", e->p.u.context_limit.used, e->p.u.context_limit.limit);
        break;
    case FE_GENERATION_CANCELLED:
        paintf(buf, cap, &n, " reason=%s", e->p.u.cancelled.reason);
        break;
    }
    return buf;
}

/* --- constructors ------------------------------------------------------- */

static fe_event fe_base(fe_type t, int gen)
{
    fe_event e;
    memset(&e, 0, sizeof(e));
    e.type = t;
    e.id = gen;
    e.ts_ms = fe_now_ms();
    e.seq = 0; /* assigned by the event source / audit log */
    return e;
}

fe_event fe_started(int gen, const char *model)
{
    fe_event e = fe_base(FE_GENERATION_STARTED, gen);
    snprintf(e.p.u.started.model, FE_MODEL_MAX, "%s", model ? model : "");
    return e;
}

fe_event fe_token(int gen, const char *text, int index)
{
    fe_event e = fe_base(FE_TOKEN_GENERATED, gen);
    e.p.u.token.text  = fstrdup(text ? text : "");
    e.p.u.token.index = index;
    return e;
}

fe_event fe_tool_call(int gen, const char *name, const char *args, int idx)
{
    fe_event e = fe_base(FE_TOOL_CALL_DETECTED, gen);
    e.p.u.tool_call.name = fstrdup(name ? name : "");
    e.p.u.tool_call.args = fstrdup(args ? args : "");
    e.p.u.tool_call.idx  = idx;
    return e;
}

fe_event fe_turn_completed(int gen, const char *text, fe_stop stop)
{
    fe_event e = fe_base(FE_ASSISTANT_TURN_COMPLETED, gen);
    e.p.u.turn_completed.text = fstrdup(text ? text : "");
    e.p.u.turn_completed.stop = (int)stop;
    return e;
}

fe_event fe_truncated(int gen, const char *partial)
{
    fe_event e = fe_base(FE_GENERATION_TRUNCATED, gen);
    e.p.u.truncated.partial_text = fstrdup(partial ? partial : "");
    return e;
}

fe_event fe_runtime_error(int gen, int code, const char *msg)
{
    fe_event e = fe_base(FE_RUNTIME_ERROR, gen);
    e.p.u.runtime_error.code    = code;
    e.p.u.runtime_error.message = fstrdup(msg ? msg : "");
    return e;
}

fe_event fe_context_limit(int gen, int used, int limit)
{
    fe_event e = fe_base(FE_CONTEXT_LIMIT_REACHED, gen);
    e.p.u.context_limit.used  = used;
    e.p.u.context_limit.limit = limit;
    return e;
}

fe_event fe_cancelled(int gen, const char *reason)
{
    fe_event e = fe_base(FE_GENERATION_CANCELLED, gen);
    e.p.u.cancelled.reason = fstrdup(reason ? reason : "");
    return e;
}

void fe_event_free(fe_event *e)
{
    if (!e) return;
    switch (e->type) {
    case FE_TOKEN_GENERATED:          free(e->p.u.token.text); break;
    case FE_TOOL_CALL_DETECTED:       free(e->p.u.tool_call.name); free(e->p.u.tool_call.args); break;
    case FE_ASSISTANT_TURN_COMPLETED: free(e->p.u.turn_completed.text); break;
    case FE_GENERATION_TRUNCATED:     free(e->p.u.truncated.partial_text); break;
    case FE_RUNTIME_ERROR:            free(e->p.u.runtime_error.message); break;
    case FE_GENERATION_CANCELLED:     free(e->p.u.cancelled.reason); break;
    default: break;
    }
}
