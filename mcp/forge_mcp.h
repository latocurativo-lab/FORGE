#ifndef FORGE_MCP_H
#define FORGE_MCP_H

#include <stddef.h>
#include <stdint.h>
#include "forge_json.h"

#ifdef _WIN32
#include <windows.h>
#else
typedef void *HANDLE;
#endif

#define FMCP_MAX_FRAME (1024u * 1024u)
#define FMCP_MAX_TOOLS 256u

typedef enum fmcp_status {
    FMCP_OK = 0, FMCP_INVALID, FMCP_NOT_APPROVED, FMCP_LAUNCH_ERROR,
    FMCP_IO_ERROR, FMCP_PROTOCOL_ERROR, FMCP_TOO_LARGE, FMCP_TIMEOUT,
    FMCP_CANCELLED, FMCP_SERVER_ERROR, FMCP_NO_MEMORY
} fmcp_status;

typedef struct fmcp_config {
    const wchar_t *executable;       /* absolute path; argv is passed verbatim */
    const wchar_t *const *arguments;
    size_t argument_count;
    const wchar_t *const *allowlist; /* caller-owned absolute executable paths */
    size_t allowlist_count;
    int caller_approved;             /* per-launch consent; must be nonzero */
    const char *client_name;
    const char *client_version;
    const char *server_id;            /* stable caller-assigned namespace */
    uint32_t timeout_ms;
    size_t max_frame_bytes;           /* 1..FMCP_MAX_FRAME */
} fmcp_config;

typedef struct fmcp_client {
    HANDLE process, job, input, output;
    uint32_t next_id, timeout_ms;
    size_t max_frame_bytes;
    char server_id[128];
    volatile LONG cancelled;
    volatile LONG active_id;
    SRWLOCK write_lock;
    int initialized;
} fmcp_client;

/* No process is created unless caller_approved and exact normalized allowlist
 * membership both pass. Returns an initialized MCP session. */
fmcp_status fmcp_start(fmcp_client *client, const fmcp_config *config);
fmcp_status fmcp_list_tools(fmcp_client *client, fj_value **result);
/* tool_name is sent as tools/call name; results remain untrusted JSON data. */
fmcp_status fmcp_call_tool(fmcp_client *client, const char *tool_name,
                           const fj_value *arguments, fj_value **result);
/* Safe to call from another thread while a request is waiting. */
fmcp_status fmcp_cancel(fmcp_client *client);
void fmcp_stop(fmcp_client *client);

#endif
