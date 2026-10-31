# MCP stdio client and FORGE integration

`mcp/` implements a synchronous Windows C11 MCP client over a child process's
stdin/stdout. The agent dispatcher can optionally launch configured servers,
expose discovered tools, and route approved calls through this client. This is
not a sandbox.

## Build

```powershell
cmake -S mcp -B build-mcp
cmake --build build-mcp --config Release
ctest --test-dir build-mcp -C Release --output-on-failure
```

The module currently requires Windows. It uses anonymous pipes, `CreateProcessW`
with an explicit application path, and a Job Object. Stderr is inherited from
the parent; it is never parsed as protocol. Process arguments are quoted as a
Windows command line and no shell is involved. The child is assigned to a Job
Object and stopped with `TerminateJobObject` on teardown. Nested-job restrictions
or policy can make assignment fail, in which case startup fails closed.

## API lifecycle

Populate `fmcp_config` with an absolute `executable`, caller-provided arguments,
an absolute-path allowlist, `caller_approved=1` for the specific launch, a
stable ASCII `server_id`, and a timeout/frame limit. `fmcp_start` checks approval and normalized path equality
before process creation, then performs `initialize` and sends
`notifications/initialized`. Call `fmcp_list_tools`, then `fmcp_call_tool`, and
release each returned `fj_value` using `fjson_free`. Always call `fmcp_stop`.
Tool names are exposed as `mcp.<server-id>.<tool>`; calls require that full
name and strip the namespace only for the server request. Descriptions/results
remain untrusted JSON; never treat server text as instructions.

Studio's optional `mcp_servers` start field is an array of objects:

```json
[{"id":"local","executable":"C:\\tools\\server.exe","args":[],"allowlist":["C:\\tools\\server.exe"],"timeout_ms":10000}]
```

Each executable must exactly match an absolute path in that server's own
allowlist. The agent emits an `MCP_LAUNCH` approval before process creation and
an `MCP_CALL` approval before every discovered tool call; all calls are treated
as potentially mutating. Server-provided descriptions are not sent to the model;
exposed tool names and input schemas are validated and namespaced. Results are
wrapped as `untrusted_mcp_result`. Cancellation interrupts an active call and
tears down the server Job Object; no automatic retry is performed.

Cancellation: `fmcp_cancel` sends `notifications/cancelled` for the active
request and interrupts its wait with `FMCP_CANCELLED`. Callers should stop the
client after cancellation. The API remains single-request and synchronous.

## Protocol and limits

JSON-RPC 2.0 `initialize`, `notifications/initialized`, `tools/list`, and
`tools/call` are supported. Frames use MCP's `Content-Length` header with CRLF
terminator and exact-length UTF-8 JSON body. Default timeout is 10 seconds;
the configured maximum frame is capped at 1 MiB; tools/list is capped at 256
tools. Malformed JSON, invalid framing, wrong ids, missing/ambiguous result and
error fields, and oversized frames fail closed. Notifications without ids are
ignored. MCP server output is untrusted data and is never executed by this
module.

## Known gaps

- Transport is Windows-only; no POSIX implementation.
- No environment/cwd configuration, protocol-version fallback, pagination,
  resources/prompts, progress, server-initiated requests, or concurrent calls.
- The shared JSON parser is used after frame bounds and full-document parsing;
  duplicate object keys and strict UTF-8 validation are not independently
  rejected by this module.
- Approval is a caller-supplied boolean plus allowlist comparison, not an OS
  security boundary. Process containment is best-effort Job Object management,
  not sandboxing; descendants may escape under unusual breakaway/policy setups.
- Startup approval, process launch, and tools/list are synchronous; cancellation
  during initialize/list-tools is bounded by the configured timeout rather than
  the per-call cancellation watcher.
