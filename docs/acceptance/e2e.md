# FORGE end-to-end acceptance

## Fixture and verifier scope

`tests/e2e/Invoke-ForgeE2E.ps1` requires a unique temporary root named
`%TEMP%\forge-e2e-<guid>\` with a `workspace\`, `verifiers\verify_fixture.exe`,
and `run\` directory. It refuses existing expected outputs or checkpoints;
create a fresh fixture instead of replaying an unsafe checkpoint.

The fixture task asks for a JavaScript integer-sum helper and `node:test` tests
for positive/negative values, empty input, and fractional-input rejection. The
native verifier's exact argument tail is `--workspace "<fixture workspace>"`;
it checks that the two expected output files exist and contain minimum marker
strings. **It does not execute the JavaScript tests.** Shells and interpreters
are never added to the agent's executable allowlist, so a structural verifier
pass must not be described as a test-suite pass.

## JSONL approval policy

The runner checks the `approval_required` protocol marker, starts llama.cpp with
the Q4_K_M model bound only to `127.0.0.1:18932` and reasoning off, waits for
HTTP `/health`, then starts `forge.exe` with a JSONL `start`. It answers only a
pending, correlated request ID and records every approval.

It permits reads only from the fixture task/package and expected output files;
listing only the workspace root and `src`/`test`; and create-only writes only to
`src/sum.js` and `test/sum.test.js`. Each `mutation:true` write is a separate
approval. Replacements, unexpected tools/paths, malformed events, and every
model-requested `EXEC` are denied and cancel the run. The verifier receives a
separate one-time `VERIFY` approval only for its exact executable and argument
tail. Server cleanup runs in `finally`. The runner writes UTF-8 JSONL payload
bytes; Forge accepts the optional initial BOM emitted by Windows PowerShell
5.1.

The protocol exposes `start`, `cancel`, and per-operation approval, but no
resume message. The runner never automatically replays a checkpoint.

## Latest result

The **2026-10-29 live multifile run passed**: local Gemma wrote both expected
files through two separately approved calls; the separately approved native
fixture verifier exited 0; Forge exited 0 with state `COMPLETED`. No model
requested executable was approved. Exact output hashes, approval ledger,
verifier output, runtime cleanup, and component tests are recorded in
`artifacts/e2e-report.md` and the temporary fixture's `run\e2e-summary.json`.

This is a successful protocol/integration smoke test, not proof that the
generated `node:test` tests execute or that arbitrary coding tasks will pass.
Do not claim generated tests passed; they were not run under the strict
no-interpreter policy. Tauri native build, sandboxing, template/model security,
and populated 16K/32K context validation remain separate acceptance gates.
