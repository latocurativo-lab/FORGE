# Gemma 4 12B Q4_K_M — populated-context protocol

This is a reproducible measurement protocol, not a claim that a configured `-c`
value is usable. Only a successful request whose API-reported `usage.prompt_tokens`
reaches the target counts as a populated context. The target ladder is 4K, 8K,
16K, 32K, 64K, and 128K tokens. The 64K/128K points are optional experiments;
they are never forced after a guard, error, OOM, or instability.

## Preconditions and safety

- Windows host: RTX 4060 Ti 16 GB, 32 GB system RAM; model file is expected to be
  7,121,861,440 bytes (SHA-256 in `models/MODEL.lock`). The script calculates
  hashes again and records them with the llama.cpp version.
- Close/stop other inference workloads yourself before running. Do not terminate
  a process to satisfy the precondition. The harness refuses to start if it sees
  llama/ollama/LM Studio processes, an active CUDA compute process, or a busy
  loopback port. Global GPU utilization is recorded but is not itself a blocker:
  on Windows WDDM it includes desktop/browser/compositor activity. This does not
  guarantee that every graphics/DirectML workload is detected; check workload
  ownership and the memory guard before interpreting results.
- A second guard runs after model load and before each evaluation. It stops when
  global free VRAM is below 1,536 MiB. The guard is on global `nvidia-smi` values,
  not process-attributed memory; the RTX 4060 Ti display desktop remains present.
- One server slot (`-np 1`), one fresh server per trial, GPU offload `-ngl 99`,
  Flash Attention on, four CPU threads, default KV type, thinking disabled, and
  16-token generation by default. No CPU/KV-quant variant is mixed into this run.
- OOM/early exit, API failure, insufficient real prompt length, or guard failure
  stops the remaining ladder. The `finally` handler terminates only the exact
  server child PID spawned for that trial; it does not find/kill other processes.

## Run

From repository root, after independently confirming the machine is idle:

```powershell
python runtime/bench/bench_context.py
```

Defaults are three replications at 4K/8K/16K/32K and one exploratory trial each
at 64K/128K. A single target can be isolated with e.g.:

```powershell
python runtime/bench/bench_context.py --targets 4096 --replicates 3
```

Output is written only under `artifacts/bench/`: a JSON run summary, per-trial
server stdout/stderr, exact prompt text, server argv, and raw completion response.
The harness generates deterministic ASCII content; `/apply-template` and
`/tokenize` on the same llama.cpp server find its actual tokenized length. The
completion response's `usage.prompt_tokens` is the authoritative populated
length. Differences between target, tokenizer count, and API usage are retained;
an underfilled request does not establish that target.

For each successful trial the JSON records start/end, process PID, configured
context, actual prompt and decoded token counts, finish reason, wall time,
llama.cpp prefill/decode timing and throughput, global GPU baseline/free before
eval and post-eval state, model/server SHA-256, build version, parameters, and raw
response path. A 500 ms global GPU sampler writes each reading to a per-trial JSON
trace and records sampled peak-used/min-free values. This is global device
telemetry (not process-attributed), and a 500 ms sampler can miss shorter peaks.
Server logs preserve load diagnostics and OOM/error evidence.

Do not infer context support from `-c` or server startup. Report each level as
measured only when its populated prompt completes with a non-error finish reason;
report failed, guarded, or inconclusive levels separately. No CPU/KV-cache
quantized fallback is part of this baseline. If later needed, run and label it as
a distinct configuration and do not combine its measurements with this table.

## Current execution

The full ladder completed on 2026-10-29; see
`artifacts/bench/context-run-20261029.md` and raw summary
`artifacts/bench/context-20261028T233347Z.json`. All 14 trials returned
API-reported prompt counts from 4,097 through 131,073 tokens, with `finish_reason`
`length` from the intentional 16-token generation cap. Three replicates passed
through 32K; 64K/128K each have one exploratory successful observation. The
largest populated request in this run was 131,073 API prompt tokens at the 128K
target, with 5,086 MiB minimum sampled global free VRAM.

The earlier blocked attempt is retained in
`artifacts/bench/context-preflight-20261029.md`. Its initial >5% aggregate GPU
utilization threshold was too strict for Windows WDDM desktop graphics; the
harness now blocks on known inference/CUDA compute processes and the existing
1,536 MiB free-VRAM floor, while logging aggregate utilization as diagnostic
telemetry. GPU memory/utilization samples remain global, not process-attributed.
