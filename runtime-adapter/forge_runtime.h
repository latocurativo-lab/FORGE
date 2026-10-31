#ifndef FORGE_RUNTIME_H
#define FORGE_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FR_STOP_TURN, FR_STOP_TOOL_CALL, FR_STOP_LENGTH, FR_STOP_OTHER,
    FR_STOP_ERROR, FR_STOP_CANCELLED
} fr_stop;

typedef struct fr_tool_call {
    char *id;                /* owned in fr_reply, borrowed in fr_message */
    char *name;              /* owned in fr_reply, borrowed in fr_message */
    char *arguments;         /* JSON text; owned in reply, borrowed in message */
    size_t arguments_len;    /* exact byte length; use instead of strlen */
} fr_tool_call;

typedef struct {
    const char *role;       /* system, user, assistant, or tool */
    const char *content;    /* NULL is sent as JSON null */
    const char *tool_call_id; /* required for role=tool; NULL otherwise */
    const fr_tool_call *tool_calls; /* assistant only; input memory is borrowed */
    size_t tool_call_count;
} fr_message;

typedef struct {
    const char *base_url;   /* must be http://127.0.0.1[:port] */
    unsigned timeout_ms;    /* per WinHTTP operation; 120000 default */
    int max_tokens;         /* -1 = no app-side output cap; model context is authoritative */
    double temperature;     /* 0.0 default */
    int thinking;           /* default 0; sent as enable_thinking */
    const char *tools_json; /* optional raw JSON array of OpenAI tool schemas */
    volatile int *cancel;   /* checked between synchronous WinHTTP operations */
} fr_config;

typedef struct {
    char *text;             /* exact decoded assistant content; never parsed as a tool call */
    char *raw_json;         /* complete response body, retained for audit */
    char *finish_reason;    /* exact server-provided finish_reason */
    fr_tool_call *tool_calls;
    size_t tool_call_count;
    fr_stop stop;
    unsigned http_status;
} fr_reply;

/* One non-streaming /v1/chat/completions request. Initialize reply to {0}.
 * On success returns 0. The call frees prior reply buffers; free it when done. */
int fr_generate(const fr_config *config, const fr_message *messages,
                unsigned message_count, fr_reply *reply,
                char *error, unsigned error_capacity);
void fr_reply_free(fr_reply *reply);

/* Decode a saved Chat Completions response (also used by unit tests). */
int fr_decode_response(const char *json, fr_reply *reply,
                       char *error, unsigned error_capacity);

/* Validate and encode messages as a JSON array. Caller frees *json with free(). */
int fr_encode_messages(const fr_message *messages, size_t message_count,
                       char **json, size_t *json_len,
                       char *error, unsigned error_capacity);

#ifdef __cplusplus
}
#endif
#endif
