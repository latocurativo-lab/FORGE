# FORGE local runtime adapter

This C11 library sends non-streaming OpenAI-compatible chat completion requests to llama-server over WinHTTP. Its standalone CMake project reuses `shared/src/forge_json.c` and `forge_util.c` from the FORGE checkout. Requests accept OpenAI `tools` JSON and set `chat_template_kwargs.enable_thinking` (off by default). The adapter retains the raw response and exact server `finish_reason`. It accepts only `http://127.0.0.1[:port]`; it does not use a proxy or support remote hosts. JSON trees use FORGE's `forge_json`; the adapter adds strict RFC 8259 syntax scanning and rejects duplicate object keys before using parsed fields.

## Build and tests

From the FORGE root in PowerShell:

```powershell
cmake -S runtime-adapter -B runtime-adapter/build
cmake --build runtime-adapter/build --config Release
ctest --test-dir runtime-adapter/build -C Release --output-on-failure
```

The complete CTest suite includes the live model test and therefore needs llama-server listening on loopback port 18932. Start the existing local binary/model from the FORGE root:

```powershell
runtime/upstream/llama.cpp/build/bin/Release/llama-server.exe -m models/gemma-4-12b-it-Q4_K_M.gguf -ngl 99 -fa on -c 4096 -t 4 -np 1 --host 127.0.0.1 --port 18932 --jinja --reasoning off
ctest --test-dir runtime-adapter/build -C Release --output-on-failure
```

The live test asks Gemma to call `echo_safe` once, verifies the returned ID/name/JSON arguments, then the test driver supplies the fixed `PING` fixture as a `role=tool` message and requests a follow-up. No command or tool proposed by the model is executed. Stop llama-server after the test.

## Stop reasons and boundaries

`stop` maps to `FR_STOP_TURN`, `length` to `FR_STOP_LENGTH`, and unknown finish reasons are retained verbatim and map to `FR_STOP_OTHER`. `FR_STOP_TOOL_CALL` is returned only when `finish_reason` is `tool_calls` and every call is valid. A missing/empty call list, malformed or partial entry, duplicate call ID, invalid or duplicate-key arguments object, or inconsistent finish reason returns `-1`, sets `FR_STOP_ERROR`, and fills the caller's error buffer. No partial call array is exposed on failure.

On a valid tool response, `fr_reply.tool_calls` owns `tool_call_count` entries. Each has an owned `id`, `name`, and `arguments` string plus `arguments_len` in bytes. `arguments` is the decoded content of the OpenAI `function.arguments` string, preserved as JSON text and verified to be a complete duplicate-free JSON object; it is not converted to Gemma tokens. `fr_reply_free()` releases each field and the array. The adapter also retains `raw_json` and the exact server `finish_reason`. The adapter never fabricates Gemma control-token strings.

`fr_message` represents history in the other direction. For `role=assistant`, it serializes every borrowed `tool_calls[]` item as OpenAI `{id,type:function,function:{name,arguments}}`; argument bytes are validated using `arguments_len` and emitted as the JSON text inside the OpenAI arguments string. For `role=tool`, `tool_call_id` must match one outstanding assistant call and `content` must be a string. It rejects duplicate IDs, unmatched/duplicate tool results, missing tool results before a later message, malformed argument JSON, and a conversation ending with unanswered calls. Tool-call/message input storage is borrowed for the duration of `fr_encode_messages`/`fr_generate`; the returned `fr_reply` remains independently owned. `fr_encode_messages()` exposes and tests the serialized OpenAI `messages` array; the caller frees its output with `free()`.

The agent callback in `agent/src/agent_loop.h` still accepts one transcript string and expects raw `<|tool_call>...<tool_call|>` in `ag_reply.text`, for `toolcall_parser.c`. This adapter now exposes the correct separate structured dispatcher contract in `fr_reply`, but the existing callback cannot consume it without changes to `agent/` (out of scope here). Do not convert these calls into pseudo Gemma tokens or pass `raw_json` to `tc_parse()`.

Timeout is 120 seconds per synchronous WinHTTP operation unless configured. Cancellation is cooperative: it is checked before sending and between blocking WinHTTP calls, so it cannot interrupt an in-progress call and may be delayed up to that operation's timeout. This is a known limitation, not immediate cancellation. Replies must be initialized as `{0}` and freed with `fr_reply_free()`.

## Local observations

The project validation record (`artifacts/runtime-validation.md`) says the selected template's one-message `Hello` probe renders an empty `thought` channel with thinking off, which is only a single-case observation. The recorded server log reports control-looking tokens that were not control-type in the GGUF and were overridden: token IDs 1 `<eos>`, 50 `<|tool_response>`, and 212 `</s>`; it also says `</s>` was removed from the end-of-generation list. This adapter does not change token metadata, strip or rewrite model output, or resolve the warning. The model/runtime/template/tokenizer must be independently audited before safety-sensitive tool use.

The live test on 2026-10-28 passed end-to-end: arithmetic returned `323`/`stop`; Gemma emitted a structured `echo_safe(PING)`/`tool_calls`; the test driver serialized that assistant call with its ID, supplied a matching `role=tool` response containing `PING`, and the model returned `PONG`/`stop`. The tool was not run. Full CTest passed 2/2 (unit plus live) with the local server ready, then the server was stopped and port 18932 had no listener. Unit coverage includes multiple ordered calls/results, null content, raw argument bytes/length, malformed/partial calls, duplicate IDs/keys, mismatch rejection, and request-message round-trip. This does not prove broad template correctness, full live stop-reason coverage, 16K/32K context, quality, or M0 completion.
