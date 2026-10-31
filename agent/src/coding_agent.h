#ifndef FORGE_CODING_AGENT_H
#define FORGE_CODING_AGENT_H

#include <stddef.h>

typedef struct {
    const char *task;
    const char *workspace;
    const char *base_url;
    const char *verify_exe;
    const char *verify_args;
    const char *const *allow_exes;
    size_t allow_exe_count;
    const char *checkpoint;
    unsigned max_turns;
    unsigned timeout_ms;
    int thinking;
    int studio_mode;
    int chat_mode;
    int resume;
    const char *mcp_servers_json;
    /* Optional continuity context for Studio sessions (validated JSON,
       see forge_parse_context_fields). Not used by CLI resume flow. */
    const char *history_json;
    const char *instructions;
    volatile int *cancel;
    int (*approval_reply)(void *context, const char *request_id,
                          const char *tool_call_id, const char *tool,
                          const char *arguments, const char *capability,
                          int mutation);
    void *approval_context;
} forge_run_options;

int forge_run_agent(const forge_run_options *options);
int forge_parse_start_json(const char *line, char **task, char **workspace,
                           char **base_url, char **checkpoint,
                            char **verify_exe, char **verify_args,
                            char ***allow_exes, size_t *allow_exe_count,
                           int *chat_mode);
int forge_parse_context_fields(const char *line, char **history_json,
                               char **instructions);
int forge_task_requests_verification(const char *task);
int forge_task_requests_workspace_change(const char *task);
int forge_parse_mcp_servers_json(const char *line, char **mcp_servers_json);
int forge_is_cancel_json(const char *line);
int forge_approval_id_matches(const char *active_request_id, const char *reply_request_id);
int forge_parse_approval_json(const char *line, char **request_id, int *approved);
int forge_cli_confirm(const char *operation, int studio_mode);
int forge_emit_json_event(const char *type, const char *message,
                          unsigned turn, const char *tool, const char *call_id);

#endif
