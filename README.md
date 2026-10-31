<div align="center">

# ⚒ FORGE

**A local-first agentic coding stack for a single consumer GPU.**
Agent harness in C, a pinned llama.cpp fork, and a Qwen3.6-35B MoE served
OpenAI-compatible on `127.0.0.1` — no cloud, no telemetry, no data ever leaves
the machine.

![platform](https://img.shields.io/badge/platform-Windows%2011-blue)
![license](https://img.shields.io/badge/code-MIT-green)
![runtime](https://img.shields.io/badge/runtime-llama.cpp%20fork-orange)
![model](https://img.shields.io/badge/model-Qwen3.6--35B--A3B-8A2BE2)
![privacy](https://img.shields.io/badge/data-stays%20local-red)

</div>

---

## Why FORGE

Big coding agents rent their brains. FORGE doesn't: one RTX 4060 Ti 16 GB runs a
35B-A3B mixture-of-experts model fast enough to drive a real, file-editing,
tool-calling agent — while every design choice is explained in a public,
arXiv-style paper instead of hidden behind a company blog post.

> 📄 **Read the paper first:** [`paper/forge.md`](paper/forge.md) — *"FORGE: A
> Local-First Agentic Coding Stack for Consumer GPUs."* Architecture, the
> 35B serving recipe, the safety model, and a full case study of how a
> misconfigured server collapsed to **1.7 tok/s** and how the tuned config got
> back to **~44 tok/s** on the same hardware.

## Highlights

- **100% local inference** — OpenAI-compatible endpoint on loopback; unplug the
  cable and the agent still works.
- **Agentic harness in C** (antirez/ds4-style): small, explicit, no framework,
  no magic. State machine + tool loop + verifier + checkpoint/resume.
- **Safety by construction** — every tool call needs explicit per-capability
  approval; exec requires an exact absolute-path allowlist; verification gates
  "done"; checkpoints marked *unsafe* are deliberately not resumable.
- **Measured, not vibed** — serving parameters tuned against real GPU metrics
  after diagnosing a catastrophic PCIe-offload spill.
- **One model at a time** — the launcher stops the previous verified server
  before starting a variant, so two GGUFs never compete for the same VRAM.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Studio (IDE)                        │
│        Tauri + React + Monaco (M5+, in progress)     │
└──────────────────────┬──────────────────────────────┘
                       │ JSONL process protocol (approvals)
┌──────────────────────▼──────────────────────────────┐
│               Agent harness (C)                      │
│   state machine · tool loop · verifier ·             │
│   checkpoint/resume · ability grants                 │
└──────────────────────┬──────────────────────────────┘
                       │ loopback HTTP (OpenAI-compatible)
┌──────────────────────▼──────────────────────────────┐
│            Runtime adapter (C)                       │
│   stop-reason disambiguation · strict JSON ·         │
│   structured tool-call validation                    │
└──────────────────────┬──────────────────────────────┘
┌──────────────────────▼──────────────────────────────┐
│            llama.cpp fork (runtime/upstream)          │
│   CUDA · KV q8_0 · --fit on · MoE expert offload      │
└─────────────────────────────────────────────────────┘
```

| Component | Path | Role |
|---|---|---|
| Agent harness | [`agent/`](agent/) | CLI + Studio JSONL protocol, checkpoints, verifier gate |
| Runtime adapter | [`runtime-adapter/`](runtime-adapter/) | WinHTTP OpenAI chat-completions client, strict stop-reason mapping |
| Tools | [`tools/`](tools/) | Filesystem primitives with per-operation approval |
| MCP | [`mcp/`](mcp/) | Local MCP stdio servers, validated launch + call |
| Studio IDE | [`studio/`](studio/) | Tauri + React + Monaco view over the headless agent |
| Runtime | `runtime/upstream/` | Pinned llama.cpp fork (git-ignored, see docs) |
| Models | `models/` | GGUF weights (git-ignored; SHA-256 verified by launcher) |
| Launcher | [`scripts/launch/start-qwen-local.ps1`](scripts/launch/start-qwen-local.ps1) | One command to serve the right model safely |
| Paper | [`paper/`](paper/) | arXiv-style architecture write-up |

## Quickstart

### Prerequisites (Windows)

- NVIDIA GPU with ≥ 16 GB VRAM (config targets RTX 4060 Ti 16 GB)
- CUDA-capable build of the llama.cpp fork in `runtime/upstream/llama.cpp/` —
  see [`docs/installation/windows-local-launcher.md`](docs/installation/windows-local-launcher.md)
- The model GGUFs in `models/` (not in git; the launcher refuses to start on a
  byte-size or SHA-256 mismatch)

### 1. Start the server

```powershell
# Abliterated variant (default): ~44 tok/s effective on 4060 Ti
.\scripts\launch\start-qwen-local.ps1 -ModelVariant Abliterated

# or the base UD variant (better factual recall, served the same way)
.\scripts\launch\start-qwen-local.ps1 -ModelVariant Base
```

The launcher verifies the GGUF (size + SHA-256), **stops any previous verified
llama-server** on the port (never foreign processes), applies the tuned serving
defaults, and waits until `GET /health` returns `ok`.

### 2. Check it

```
GET http://127.0.0.1:18932/health          → {"status":"ok"}
POST http://127.0.0.1:18932/v1/chat/completions
```

Standard OpenAI-compatible API. Inside Docker use
`http://host.docker.internal:18932/v1` — the server binds to `127.0.0.1` only,
so it is never reachable from the LAN.

### 3. Point an agent/IDE at it

Example OpenCode provider (`opencode.json`):

```json
{
  "provider": {
    "forge": {
      "npm": "@ai-sdk/openai-compatible",
      "name": "FORGE local",
      "options": { "baseURL": "http://127.0.0.1:18932/v1" },
      "models": {
        "qwen36-uncensored": { "name": "Qwen3.6 35B A3B (Abliterated)" },
        "qwen36-local":      { "name": "Qwen3.6 35B A3B (Base)" }
      }
    }
  }
}
```

Any tool that speaks OpenAI chat completions works the same way (Cursor fork
configs, Aider `--openai-base-url`, a 20-line script…). One prompt to give an
agent against this endpoint lives in
[`docs/installation/agent-prompt-example.md`](docs/installation/agent-prompt-example.md).

## Models

| Variant | File | Size | SHA-256 (trunc.) | Role |
|---|---|---|---|---|
| **Abliterated** *(default)* | `Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ3_M.gguf` | 15.4 GB | `4dd9a27c…be0c0` | Default launcher + `forge/qwen36-uncensored` |
| **Base** (fallback) | `Qwen3.6-35B-A3B-UD-IQ3_XXS.gguf` | 13.2 GB | `9c964e65…b88fbe` | Registered as `forge/qwen36-local` |

**We disclose:** the Abliterated (community-edited, aggressive-refusal-removal)
variant hallucinates on factual tasks more than the base model. It is the
default because FORGE is a coding agent (tool results dominate the context),
but pick **Base** for fact-heavy workflows. The launcher only starts **one**
variant at a time.

## Serving recipe (and why)

The published defaults were tuned on RTX 4060 Ti 16 GB + 31 GB system RAM; the
paper's §4 walks through the full case study. In short:

| Setting | Value | Why |
|---|---|---|
| context `-c` | 65536 | 128K forced KV caching caused PCIe spill to host RAM ⇒ 1.7 tok/s |
| `-ngl` | auto (`0` → default) | forcing `99` with `--fit on` aborts the fit and misbalances layers |
| KV cache | `q8_0` K+V | ~14 GB→~9 GB KV at 64K, negligible quality loss |
| `--n-cpu-moe` | 25 (Abliterated) / 20 (Base) | keeps hot $(Q,K,V,O)$ + router on GPU, cold experts in host RAM |
| `--moe-cache-mib` | 1024 | 1 GiB hot-expert cache on GPU despite CPU offload |
| `--fit on` | on | llama.cpp auto-balances GPU budget; do **not** pair it with a forced `-ngl 99` |
| sampling | temp 0.6 · top-p 0.95 · top-k 20 | Qwen recommended decoding defaults |
| thinking | `--reasoning on`, DeepSeek format, `preserve_thinking` | shared community chat template for both variants |

Rule of thumb we learned the hard way: **a spill is a config bug, not a model
bug.** If FORGE runs at single-digit tok/s, suspect layer placement before
drawing conclusions about the model.

## Safety model

Unlike most "local agent" demos, FORGE grants nothing implicitly:

- workspaces start with **zero** tool permissions; each capability (`READ`,
  `LIST`, `WRITE`, `EXEC`, `VERIFY`, `MCP_*`) needs a per-operation prompt grant;
- `run_command` takes only an **exact absolute executable + argument tail**,
  from an explicit allowlist — no shell, no interpreters;
- completion is gated by a **caller-supplied verifier** (a real build/test exit
  code), not by the model saying "done";
- checkpoints are marked `unsafe` before every side-effect and are then
  deliberately non-resumable; interrupted work is reported as *UNKNOWN*, never
  replayed behind your back.

See [`agent/README.md`](agent/README.md) and the paper's §5 for the full threat
model, including known non-goals: this is not a sandbox, and filesystem
TOCTOU/inspection gaps are documented, not hidden.

## Repository layout

```
agent/              harness in C (CLI + Studio JSONL)
runtime-adapter/    OpenAI-compatible client library (WinHTTP)
tools/              approved filesystem/exec tool library
mcp/                local MCP stdio server layer
studio/             Tauri + React + Monaco IDE (M5+)
shared/             forge_json / forge_util
scripts/            launcher, packaging, sandbox helpers
models/             GGUF + chat template (git-ignored)
runtime/upstream/   llama.cpp fork @ pinned commit (git-ignored)
docs/               architecture, protocols, benchmarks, plans
artifacts/          audit reports, benchmark evidence, gate records
LICENSES/           third-party license notice
paper/              the arXiv-style write-up
```

## Status & roadmap

Milestone plan lives in [`docs/plans/`](docs/plans/).

- **M0** ✅ environment audit, llama.cpp CUDA fork, first measured generation
- **M1** ✅ tool-calling protocol + stop-reason disambiguation (`stop` ≠ `length` ≠ rejected ≠ `tool_calls`)
- **M2** ✅ headless CLI: state machine, verifier gate, checkpoint/resume
- **M3** 🔶 sandboxed tools (filesystem approval flow exists; TOCTOU notes in docs)
- **M4** 🔶 MCP client/server (stdio path validated with fake-server suite)
- **M5** ⏳ context engine (project + session + long-term memory)
- **M6** ⏳ Studio IDE + portable packaging

## Honest limitations

- **Windows-only** currently; POSIX port is not started.
- The **Studio** IDE is in progress; today's surface is the CLI.
- There is **no OS-level sandbox** inside the agent process — the approval and
  allowlist layers are defenses, not isolation. Read
  [`tools/README.md`](tools/README.md) before trusting it with real work.
- System RAM is the real ceiling at 35B with KV in VRAM; with other heavy apps
  open, expect pressure. One model at a time.

## License

Code is **MIT** (see [`LICENSE`](LICENSE)). Model weights keep their own
licenses: Qwen3.6 derivatives under the Qwen/Apache-2.0 terms; community GGUF
edits carry their own upstream notice; llama.cpp is MIT and remains so in the
fork. Full third-party table in [`LICENSES/NOTICE.md`](LICENSES/NOTICE.md).
FORGE does **not** redistribute model weights — the launcher expects you to
place them in `models/` and verifies them by hash.

---

<div align="center">

**Manuel Cavallaro** — [GeekAndHack.com](https://geekandhack.com) · [digithole.com](https://digithole.com)
Local-first tuning war stories: [`paper/forge.md`](paper/forge.md) ·
Built with: llama.cpp (MIT, pinned fork) · ds4 (design reference, MIT) · WinHTTP · CUDA

</div>
