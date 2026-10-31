# Gemma 4 12B Q4_K_M — populated-context run

Run summary: `context-20261028T233347Z.json` (UTC timestamp); llama.cpp
`0.8.0-dev`, build `1`, commit `5220213`. Model SHA-256:
`0a270ec9fe6b34f4a0d33992b6135117b484ebc4766ab76b51d4ae8c457e4c42`.
Hardware reported by the runner: RTX 4060 Ti 16 GB, 32 GB RAM. Settings:
`-ngl 99`, Flash Attention on, one slot, four threads, default KV type,
thinking off, temperature 0, seed 42, and 16 generated tokens.

All 14 trials finished with runner status `ok`; the API reported one more prompt
token than the tokenizer count at every point (the target payload count plus one
chat/template token). No trial was underfilled or stopped by the VRAM guard.
`finish_reason=length` is expected because generation was deliberately capped at
16 tokens; this is a context/prefill performance probe, not a quality or task
completion evaluation.

| Target | Replicates | API prompt tokens | Median wall | Median prefill | Median decode | Max sampled used VRAM | Min sampled free VRAM |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 4K | 3 | 4,097 | 3.03 s | 1,642 tok/s | 30.3 tok/s | 8,903 MiB | 7,205 MiB |
| 8K | 3 | 8,193 | 5.51 s | 1,644 tok/s | 30.2 tok/s | 8,969 MiB | 7,139 MiB |
| 16K | 3 | 16,385 | 10.91 s | 1,579 tok/s | 30.3 tok/s | 9,101 MiB | 7,007 MiB |
| 32K | 3 | 32,769 | 24.47 s | 1,373 tok/s | 29.2 tok/s | 9,328 MiB | 6,780 MiB |
| 64K | 1 exploratory | 65,537 | 59.59 s | 1,113 tok/s | 27.6 tok/s | 9,873 MiB | 6,235 MiB |
| 128K | 1 exploratory | 131,073 | 162.87 s | 810 tok/s | 24.5 tok/s | 11,022 MiB | 5,086 MiB |

The 128K trial is the largest populated context observed in this run. 64K and
128K have one observation each, so they establish a successful measured request,
not repeatability or broad production stability. VRAM values are global
`nvidia-smi` samples at 500 ms cadence (not process-attributed; short peaks may
be missed). Desktop WDDM graphics activity remained present; the preflight
blocks CUDA compute clients and low free VRAM, while recording rather than
rejecting aggregate graphics utilization.

The model is observed to accept these populated prompt sizes with the recorded
settings. This run does not validate the model's template/tokenizer against the
official source, establish security of the flagged GGUF, or prove useful response
quality at very long contexts. Per-trial prompts, raw responses, server logs,
commands, and GPU traces are adjacent to the JSON summary under
`artifacts/bench/`.
