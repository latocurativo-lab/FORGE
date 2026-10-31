# Gemma 4 populated-context benchmark — preflight result

Date: 2026-10-29 (local host clock). No inference trial was launched.

## Initial inspection

- GPU: NVIDIA GeForce RTX 4060 Ti, 16,380 MiB reported total.
- `nvidia-smi` observations: 2,563 MiB used / 13,817 MiB free at first read;
  a second read showed 2,551 MiB used / 13,829 MiB free and 45% GPU utilization.
  (Minor variation between reads is normal.)
- LM Studio (`LM Studio.exe --run-as-service`) was running; the GPU process list
  also showed a Pinokio Miniforge `python.exe launch.py` process. These are
  pre-existing processes and were left untouched.
- The free-memory floor of 1,536 MiB was not the blocker: it was met at the
  sampled instant. The required idle-workload precondition was not met (45%
  utilization), so running would have contaminated results and could interfere
  with another workload. The runner's stricter >5% idle guard would refuse too.
- No llama.cpp benchmark server was started by this attempt. No process was
  killed. Therefore 4K/8K/16K/32K/64K/128K are all **not tested** here, not
  failed context sizes and not established as supported.
- The harness itself was then invoked as a safe preflight. It refused before
  creating any trial/server process: LM Studio was still present and the GPU
  reported 20% utilization / 13,557 MiB free. Exact machine-readable record:
  `artifacts/bench/context-20261028T232448Z.json` (timestamp is UTC; host date
  was Oct 29). This is evidence of a guard refusal, not a benchmark run.

## Evidence limitations

This is a preflight only; its GPU figures are instantaneous global telemetry,
not benchmark baseline/peak measurements. There are no prompt token counts,
decode timings, finish reasons, or OOM results from this attempt. Run
`python runtime/bench/bench_context.py` only after independently clearing other
inference activity; the harness saves raw logs and results under this directory.
