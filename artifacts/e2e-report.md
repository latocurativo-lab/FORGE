# FORGE E2E report — 2026-10-29

## Outcome: live multifile task completed; verifier passed

The current `build-agent/Release/forge.exe` (SHA-256
`31053387606DCE053735B443AE7ED866014C4709D8CA00D2BFE54AB3FAB8D846`) completed
a real task with the local Gemma 4 Q4_K_M runtime over the JSONL approval protocol.
The fixture was newly created outside the repository at:

`%TEMP%\forge-e2e-6d10f53b888240bd8e725b70634384af\workspace`

The task requested `src/sum.js` plus `test/sum.test.js`. The run took **21.2 s**;
Forge exited **0** with state `COMPLETED`. Two create-only writes were each
approved separately, and the native verifier was separately approved as
`VERIFY` with the exact executable and argument tail. The verifier exited 0 and
printed `fixture verifier PASS`. There were **2 tool calls**, **3 explicit
mutation approvals** (two writes and the separate verifier), **0 model-requested
EXEC approvals**, and no denied tool requests.

Output SHA-256:

| File | Bytes | SHA-256 |
|---|---:|---|
| `src/sum.js` | 321 | `AD2598A347E9B8B2A1F76209E668DB7383C99E3407884F1A208BA1DD5125E06F` |
| `test/sum.test.js` | 602 | `00B2CECE0CD69EC0A8D9201860648D3D64976BD250FDD50D7104F3F0C891391E` |

The native verifier source/binary SHA-256 is
`B734864667DD127CB6BFC79EDA3B0B3862D53EE764F33457C3292789EB0FC23D`. This
verifier checks that both files exist and contain minimum marker strings. It is
a structural fixture verifier, **not** execution of the generated JavaScript
tests. Node/shell/interpreter execution was intentionally not allowlisted and
the `node:test` suite was not run; no claim is made that those tests passed.

The checkpoint is marked unsafe (the session performed writes and verification)
and must not be resumed/replayed. The previous failed fixtures/checkpoints were
left untouched. The runtime was bound only to `127.0.0.1:18932`; cleanup was
confirmed, and a subsequent check found no `llama-server`, `forge`, or listener
on that port. No screenshot applies to this headless run.

Detailed evidence is under the temporary fixture's `run\` directory:
`e2e-summary.json`, `forge.stdin.jsonl`, `forge.stdin.bytes`,
`forge.stdout.jsonl`, `llama-server.stderr.log`, and `llama-server.stdout.log`.

## Relevant fixes found by the acceptance run

1. Moved the 1 MiB JSONL startup buffer from the Windows process stack to heap;
   added `test_studio_start` regression coverage.
2. Accepted an optional UTF-8 BOM on the initial JSONL record because Windows
   PowerShell 5.1 adds one to redirected stdin.
3. After each correlated Studio approval, granted the matching internal tool
   capability bit; approval is still required for every operation.
4. Normalized `/` to `\\` for workspace-relative paths before Windows tool
   dispatch; command arguments are not rewritten.
5. Fixed the E2E approval policy so `mutation:true` is permitted only for the
   expected create-only writes and the exact separate verifier request.

## Component verification

| Component | Build / tests |
|---|---|
| Agent | Build passed; **7/7 CTests passed** |
| Tools | Build passed; **1/1 CTest passed** |
| Runtime adapter | **2/2 CTests passed** in the preceding live-ready run; current unit test **1/1 passed** |
| Studio frontend | Build passed; **3/3 tests passed** |
| Tauri native desktop | **Not built**: Rust/Cargo is unavailable in this environment |

The E2E proves the real model → approved tool calls → correlated results →
follow-up → separately approved verifier path. It does not establish general
task correctness, sandboxing, or native desktop readiness. No commit was made.
