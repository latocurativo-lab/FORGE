# FORGE C agent

Windows CLI build:

```powershell
cmake -S agent -B build-agent -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent --config Release
ctest --test-dir build-agent -C Release --output-on-failure
```

## CLI

```powershell
forge run --task "Implement the change" --workspace C:\work\fixture `
  --verify-exe C:\tools\ctest.exe --verify-args "--test-dir build --output-on-failure" `
  --allow-exe C:\tools\ctest.exe --checkpoint C:\work\fixture\agent-checkpoint.json
```

The final answer is not verification. A run completes only when the caller
provided a verifier executable and exact argument tail, the executable is in
the exact allowlist, the caller approves both execution and mutation at the
prompt, and it exits 0 before the configured finite timeout. Otherwise the
session remains failed/unverified. `--max-turns` and `--timeout-ms` are bounded
positive integers. `--base-url` accepts only the runtime adapter's loopback
endpoint. Thinking is off by default.

`--allow-exe` can be repeated. `run_command` accepts only an exact allowlisted
absolute executable and its displayed Windows argument tail; the agent never
invokes a shell. Shells/interpreters are explicitly denied. Even direct
allowlisted executables inherit the caller's token, environment, and network:
the tools library is not a sandbox. Verify/test commands should be narrow
trusted binaries, not general command interpreters.

The workspace starts with no tool permissions. On the first call for each
capability the CLI asks to grant that capability, then asks for each
operation. Reads/list/search use the corresponding read/list capability.
Writes use `ft_write` (create-only) or `ft_replace_approved` (exact expected
original bytes required). Exec uses `ft_run_approved`, the exact allowlist,
per-invocation approval, and a separate mutation approval. A denied action is
returned as a structured tool result; tool execution errors and unknown
outcomes are recorded and stop the run without an automatic retry.
Workspace tool paths are relative to the opened root; both `/` and `\\` are
accepted from the model and normalized to Windows separators before dispatch.

Only explicit command verification is currently implemented; there is no
generic inferred “task done” or automatic criteria verifier. Supply the exact
test/build executable and arguments. A nonzero exit, timeout, missing verifier,
generation length stop, unknown stop, malformed runtime response, or native
Gemma tool marker without a structured OpenAI tool stop cannot complete a task.

Checkpoints are written before model calls. Immediately before any tool call
or verifier execution, a checkpoint is marked unsafe. Unsafe and terminal
checkpoints are intentionally not resumable, preventing accidental replay of
non-idempotent actions. A safe checkpoint is resumable with the same explicit
task/workspace and starts before tool side effects.

## Studio JSONL

With no process arguments, `forge.exe` reads one UTF-8 JSON line from stdin
(an optional UTF-8 BOM is accepted on this first record for Windows PowerShell
redirected-stdin compatibility):

```json
{"type":"start","task":"...","workspace":"...","allow_exe":["C:\\tools\\ctest.exe"],"verifier":{"executable":"C:\\tools\\ctest.exe","args":"--test-dir build --output-on-failure"},"mcp_servers":[]}
```

Studio may configure up to eight local MCP stdio servers in `mcp_servers`; each
entry contains `id`, absolute `executable`, string-array `args`, an absolute
`allowlist` containing that exact executable, and optional `timeout_ms`. The
agent approves each process launch before starting it, discovers tools once,
exposes only validated `mcp.<id>.<tool>` names, and requests separate explicit
approval for every call. Every MCP call is treated as potentially mutating.
Server descriptions are not inserted into the model prompt; call results are
marked untrusted. See `docs/protocols/mcp.md`. This process protocol is not a
sandbox.

After startup one dedicated reader owns stdin for the whole run. It accepts
`{"type":"cancel"}` or an approval reply while running. Stdout is only JSONL
events (`start`, tool results, assistant, verification, cancellation, and exit);
diagnostics go to stderr. Each workspace operation and direct `run_command`
execution pauses before dispatch and emits an `approval_required` event with a
unique `request_id`, correlated `tool_call_id`, tool name, parsed arguments,
capability (`READ`, `LIST`, `WRITE`, `EXEC`, `MCP_LAUNCH`, `MCP_CALL`, or
`VERIFY`) and `mutation` flag. Answer only
the currently pending request:

```json
{"type":"approval","request_id":"forge-...","approved":true}
```

`approved:false`, a stale/mismatched ID, malformed reply, or the finite
120-second timeout never authorizes an operation (timeout is deny-by-default).
Only one approval can be pending. `cancel` interrupts an approval wait, model
generation, and an in-flight verifier/tool process. A verifier has its own separate approval event with
capability `VERIFY`; it is not implicitly authorized by an earlier exec/tool
approval. Verifier and tool executables must still be listed exactly in the
`allow_exe` string array. The `verifier.args` value is a Windows argument-tail
string, matching the CLI `--verify-args` contract.
Studio gets a uniquely named checkpoint in the OS temp directory unless the
start message specifies one. Generation uses a cancellation watcher that closes
the active WinHTTP request handle; cancellation latency is typically within one
5 ms watcher interval plus WinHTTP's return/cleanup time. Tool execution polls
the cancel flag at 10 ms intervals and terminates its Windows Job Object,
including descendants, before joining output-drain threads. Tool cancellation
is an unknown outcome: the checkpoint stays unsafe, the run stops, and no
automatic retry/replay occurs. A timeout remains `FT_TIMEOUT`, not cancellation,
and also remains an unknown tool outcome.

No approval is ever granted implicitly in Studio. Callers that do not implement
the approval handshake will receive no successful tool execution; unapproved
operations are denied, and verification will remain incomplete.

## Tests and known limits

`test_cli_protocol` exercises start JSON parsing (including the executable
allowlist and verifier), approval-reply validation, decoded structured tool-call response → assistant
tool_calls history → correlated `role=tool` result → follow-up serialization,
including invalid call-ID correlation, and validates emitted JSONL event
shape and `start`/`cancel` JSON parsing. `test_cancellation` runs a real local
slow HTTP endpoint and a real descendant-spawning Windows command: generation
abort and process-tree termination each complete below the 2 s test bound; the
late marker is still absent after 6.2 s. It also races cancellation with a
positive approval callback and confirms no write occurred and the checkpoint
remains unsafe. Current Agent CTest is 9/9 (including the fake-runtime MCP
launch/call/verifier round-trip); tools CTest is 1/1. The MCP library's fake
server protocol suite is independently 1/1.
Runtime adapter unit CTest is 1/1. The full runtime-adapter CTest additionally
contains a live test; it fails here because `http://127.0.0.1:18932` is not
listening (WinHTTP 12029). Consequently the live Gemma `echo_safe(PING)`
tool-call → correlated response → follow-up and a real multi-file fixture task
were not run. No tool was executed without approval. This agent does not claim
process isolation or eliminate filesystem TOCTOU risks documented in
`tools/README.md`. Cancellation cannot undo effects a tool performed before the
cancel request; this is why an interrupted process is reported as UNKNOWN and
its checkpoint is not resumable. The approval race is integration-tested through
the agent approval callback, and protocol tests check exact active-ID matching
(including stale and empty IDs). Cancellation is cooperative at dispatch boundaries for filesystem calls;
once an individual synchronous filesystem operation has begun it is not forcibly
interrupted. MCP tool calls use the client cancellation watcher; MCP initialize
and tool discovery remain bounded by the configured timeout.
