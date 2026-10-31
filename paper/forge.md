---
title: "FORGE: A Local-First Agentic Coding Stack for Consumer GPUs"
author: "Manuel Cavallaro — GeekAndHack.com / digithole.com"
date: "October 2026"
license: "Code MIT; model weights under their own upstream licenses (see LICENSES/NOTICE.md)"
---

# FORGE: A Local-First Agentic Coding Stack for Consumer GPUs

**Abstract.** Agentic coding systems today almost universally assume a frontier
API behind them. FORGE is an experiment in removing that assumption entirely:
a Windows-native stack in which a C-language agent harness (in the explicit,
low-magic style of antirez's *ds4*) drives a quantized 35B mixture-of-experts
model, Qwen3.6-35B-A3B, served over an OpenAI-compatible loopback endpoint by a
pinned llama.cpp fork — all on a single RTX 4060 Ti with 16 GB of VRAM and 31 GB
of system RAM. No component in the loop calls a server. This paper documents
the four-layer architecture (Studio, Agent, Adapter, Runtime), a serving recipe
derived from measurements rather than defaults; a **case study of a
catastrophic PCIe-offload spill** that reduced generation to 1.7 tok/s and the
configuration path that recovered ~44 tok/s on identical hardware; and the
agent's treat-as-hostile safety model: per-capability approvals, exact
executable allowlists, a caller-supplied verifier that gates task completion,
and checkpoint semantics that refuse to resume after unknown side effects. We
close with honest limitations — including measured behavioral differences
between the base and abliterated model variants under identical serving
conditions — because a local-first stack that hides its failure modes is not
local-first, it is just fragile. Underneath the engineering sits a plain
economic argument for owning the intelligence layer instead of renting it:
rented inference re-prices upward, is revocable at Terms-of-Service speed,
and its context is, for a coding agent, the codebase itself.

---

## 1. Introduction

Cloud-hosted coding agents have an implicit contract: your source code, your
file trees, your prompts and your chain-of-thought all cross a network to a
machine you do not control. FORGE rejects that contract on principle. The
design question is narrower than "can a local model do what a frontier API
does?" — the answer is plainly *not always* — and closer to:

> **Given a consumer GPU, what is the strongest agentic loop we can build such
> that every byte the agent sees stays on the machine, and every action the
> agent takes is explicitly approved?**

Three decisions define the project:

1. **Harness before IDE.** An IDE is a view; the agent must first exist
   headless with a strict protocol (M2 gate: a multi-file task with 20–30 tool
   calls completed with no human "continue").
2. **llama.cpp fork, pinned.** All runtime knowledge (tokenizer, chat
   template, stop reasons, caching behavior) is pinned to one commit and
   modified only through a patch series; behavior differences across upstream
   releases cannot silently change the agent's contract.
3. **Treat model output as untrusted input.** Tool results, MCP descriptions
   and page contents are data. Approvals, allowlists and verifiers are the
   only paths to side effects.

The paper is written as lab notes for that position. §3 gives the
architecture; §4 the model selection and serving recipe with the spill case
study; §5 the safety model as actually implemented and shipped; §6 the
integration surface for external consumers (IDEs, agents, Docker);
§7 the limitations we are aware of and disclose.

### 1.1 Why local: the democratization argument

It is tempting to frame cloud-hosted coding agents as merely *inferior on
privacy*, a trade-off someone else mitigates with data-retention toggles. We
register three stronger objections, because they are structural rather than
configurable:

**Cost trajectory.** Rented inference is priced, advertised and re-priced by
the seller. Each frontier refresh has historically reset marginal cost
upward while throttling concurrency, meaning a team's agent bill scales like
a headcount line item. The user already possesses potentially idle consumer
accelerators whose marginal server-hour is $0. The economics are not even
close once the serving layer is engineered for that hardware (§4).

**Revocability.** A rented brain is a revocable grant: terms change, models
are deprecated, regions are rate-limited, accounts are flagged without a
debuggable appeal path. When the agent is load-bearing, this is an
unauditable dependency on a Terms of Service the consumer never negotiated.
Owning weights, runtime and checkpoint format removes the class of outage,
not merely its frequency.

**Data egress.** For a coding agent, context *is* the codebase. Prompts carry
file contents, dependency trees, architecture decisions, and
secrets-adjacent material out of the machine's controllable envelope into an
event stream that is immutable, uninspectable and undeleteable by the user.
And its *usage* side quietly trains future models on that traffic; the
provider's incentive is to use it, with no contractual obligation to care
whether it should. You cannot audit a data flow you cannot see.

### 1.2 Why the generalist stacks keep breaking, and where the fix belongs

llama.cpp, vLLM and other *generalist* serving layers must satisfy every
model family, every template and every upstream refactor simultaneously.
Generality forces politeness, and politeness at the serving boundary makes
the agent's contract *ambiguous*: chat templates drift across releases;
`finish_reason` collapses "turn ended", "generation truncated" and "tool
requested" into similar byte patterns; tool-call JSON arrives partial and the
harness upstack fills the gap with "please continue" stalls or invented
arguments.

We argue this is unwinnable at the model layer and entirely winnable at the
**contract layer** — which is why FORGE is structured the way it is: pinned
runtime modified only through patch series (§3.1), strict stop-reason
disambiguation with no partial exposure on failure (§3.2), verifier-gated
completion and fail-closed checkpoints (§3.3, §5). Interruptions still occur;
the difference is that an interruption is recorded as UNKNOWN and stops the
run, rather than being papered over by a generalist harness's optimism.

## 2. Design principles

- **Explicit over clever.** The harness is plain C — no framework, no
  dependency-injection, no plugin registry. A single file per concern
  (`agent_loop`, `toolcall_parser`, state machine).
- **Everything measured.** Every serving parameter in §4 exists because a
  metric moved when it changed. Defaults we could not justify by measurement
  are not shipped.
- **Deny by default.** Approvals time out to *deny*, not to *grant*.
- **Unknown ≠ failed ≠ completed.** A canceled or interrupted tool execution
  is an *unknown outcome* the run must not silently paper over.
- **A checkpoint is not an undo log.** Checkpoints exist for *resuming safe
  work*, not for *undoing effects*. If a side effect happened, the checkpoint
  is unresumable.

## 3. Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Studio (IDE)                        │
│          Tauri + React + Monaco (M5+)                │
└──────────────┬───────────────────────────────────────┘
               │ JSONL process protocol; stdin = commands
               │ + approvals; stdout = events only
┌──────────────▼───────────────────────────────────────┐
│               Agent harness (C)                      │
│  state machine · tool loop · verifier gate ·         │
│  checkpoint/resume · capability grants · MCP layer   │
└──────────────┬───────────────────────────────────────┘
               │ loopback HTTP (WinHTTP, OpenAI-compatible)
┌──────────────▼───────────────────────────────────────┐
│            Runtime adapter (C)                       │
│  strict JSON scan (RFC 8259, dup-key rejection);     │
│  finish_reason → stop-class mapping; structured      │
│  tool-call validation (ids, args, ordering)          │
└──────────────┬───────────────────────────────────────┘
┌──────────────▼───────────────────────────────────────┐
│            llama.cpp fork (runtime/upstream)          │
│  pinned commit · CUDA build · KV q8_0 · --fit on ·   │
│  MoE CPU offload (--n-cpu-moe / --moe-cache-mib)     │
└─────────────────────────────────────────────────────┘
```

### 3.1 Runtime: the pinned fork

`runtime/upstream/` holds llama.cpp at a fixed commit; every FORGE-specific
modification lives in `runtime/patches/` as a `git am`-able series. This
separates *our* contract (§3.2) from llama.cpp's evolution. A known, logged
quirk of the selected GGUF/template combination was that token IDs 1
(`<eos>`), 50 (`<|tool_response>`) and 212 (`</s>`) arrived as
control-**looking** but not control-type tokens and were overridden; we
recorded it rather than "fixing" it in the adapter, and flag the whole
model/template/tokenizer chain for audit before safety-sensitive tool use
(§7).

### 3.2 Adapter: stop reasons are load-bearing

The adapter maps exactly: `stop` → `FR_STOP_TURN`, `length` →
`FR_STOP_LENGTH`, `tool_calls` (with a *fully valid* call array) →
`FR_STOP_TOOL_CALL`; unknown finish reasons are preserved verbatim as
`FR_STOP_OTHER` and are never silently promoted to "finished". A missing
call list, malformed or partial entry, duplicate call ID, or invalid/duplicated
JSON keys yields an error (`-1`, `FR_STOP_ERROR`) and exposes **no partial call
array**. The agent cannot "accidentally" act on half a tool call: this is the
difference between *finished the turn* and *finished the task*.

### 3.3 Agent: loop, verifier, checkpoints

The loop is a state machine, not a ReAct string. Model-driven tool call →
correlated `role=tool` history entry → next generation; approval events
interrupt the loop at dispatch boundaries. Completion has one rule: **the
final answer is not verification**. A run is complete only when a
caller-provided verifier (exact executable, exact argument tail) exits 0
inside a finite timeout. Generation-length stops and malformed runtime
responses are hard-failures, not retries.

Checkpoints are written before model calls and marked *unsafe* immediately
before any tool call or verifier execution. Unsafe and terminal checkpoints
are deliberately non-resumable — the state after a side effect is known-unknown
and replaying "before tool side effects" would repeat them. Safe checkpoints
resume *before* tool side effects, with the same task/workspace.

### 3.4 MCP

MCP servers are local stdio processes. Launch approval, one-time tool
discovery, and — separately — per-call approval for every invocation; each
call is treated as potentially mutating. Server *descriptions* are never
inserted into the prompt (prompt-injection surface reduction); call results
are marked untrusted in the transcript. This is documented behavior, not an
implementation detail: `mcp.<id>.<tool>` names are validated before exposure
to the model.

### 3.5 Studio

Tauri + React + Monaco, behind the same JSONL stdin/stdout protocol as the
CLI. Notably, Studio inherits the same denial semantics: a caller that does
not implement the approval handshake simply receives no successful tool
executions — unapproved operations deny and verification stalls. The IDE
cannot bypass the harness; there is no "trust me" mode.

## 4. Model selection and serving on 16 GB

### 4.1 Variants

| Property | Abliterated (default) | Base (fallback) |
|---|---|---|
| File | `Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ3_M.gguf` | `Qwen3.6-35B-A3B-UD-IQ3_XXS.gguf` |
| Size | 15.4 GB | 13.2 GB |
| SHA-256 | `4dd9a27c…be0c0` | `9c964e65…b88fbe` |
| Quantization | IQ3_M | UD IQ3_XXS |
| Role | `forge/qwen36-uncensored` (default) | `forge/qwen36-local` |

The Abliterated variant is a community aggressive-refusal-removal edit; we
distribute *verification and serving configuration*, not weights, and we
publish the trade-off openly: in side-by-side use the abliterated variant
**hallucinated more on factual questions** than the base model under identical
serving conditions. FORGE keeps it as the default because the workload is
tool-driven (grounding comes from tool output, not memory) and refusal-free
generation is materially more useful in an editing loop — but the paper is a
better place to admit that than a hidden README footnote.

### 4.2 Serving configuration

Both variants share one serving config and one chat template
(`qwen3.6-chat-template.community.jinja`, rendered with
`preserve_thinking: true`, reasoning format `deepseek`):

```text
-c 65536  -t 4  -np 1
--host 127.0.0.1 --port 18932
--jinja --chat-template-file <template> --chat-template-kwargs {"preserve_thinking":true}
--reasoning-format deepseek --reasoning on
--cache-type-k q8_0 --cache-type-v q8_0 --fit on
--temp 0.6 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0
--n-cpu-moe 25 (Abliterated) | 20 (Base)
--moe-cache-mib 1024
```

Estimates behind the choices, from the GGUF itself: expert weights ≈ **0.27 GiB
per layer** (IQ3_M), so `--n-cpu-moe 25` keeps ≈ 6.75 GB of expert tensors out
of the 16 GB VRAM budget while the router and attention projections stay
resident; KV q8_0 at this context is ≈ **96 KiB/token** (n_layer 48). The
`--fit on` flag lets llama.cpp reconcile the residual budget; that
reconciliation is exactly what breaks when `-ngl` is forced (§4.3).

### 4.3 Case study: the 1.7 tok/s spill

During the first Qwen test session the model was served with
`-ngl 99` (a value lifted from the earlier 12B Gemma config) and a 131072-token
context. The observed result was **1.7 tok/s**. The verified prompt log wrote
"the write tool stopped working" — the correct diagnosis was that nothing about
*tooling* had changed; the *server* had exceeded the VRAM budget and every
forward pass was forced through PCIe.

The recovery path, in the order we applied it:

1. Stop forcing `-ngl 99` while `--fit on` is active; the forced value makes
   the fit *abort* rather than balance, leaving layer placement misbalanced.
2. Reduce context 131072 → 65536 (KV q8_0 ≈ 96 KiB/token ⇒ ~6 GB instead of
   ~13 GB of KV for the same usable conversational window at typical agent
   depth).
3. Move cold experts to host RAM: `--n-cpu-moe 25` (Abliterated) / `20` (Base),
   keeping a 1 GiB hot-expert cache on GPU.
4. Keep KV q8_0 for K **and** V.

Result on the same hardware, same session, same prompt suite: **~44 tok/s**
effective — a 26× throughput recovery — with the abliterated expert-offload
exactness preserved (20/48 layers of MoE work in system RAM instead of spilling
over PCIe at load time).

Three lessons, generalized:

- **Spill is a configuration bug, not a model deficiency.** At 1.7 tok/s it is
  tempting to buy a bigger GPU; at 44 tok/s, 16 GB is comfortably usable.
- **Hard-coded values copied from one model (~12B dense) do not transfer to
  another (35B MoE).** The auto-balancing path exists precisely so ranges
  remain valid when the weights change under it.
- **An agent's perceived reliability tracks its inference rate.** The same
  stack that "felt broken" at 1.7 tok/s felt fully agentic at 44.

### 4.4 Smoke-test method

The launcher exposes one binary contract but three checks: byte-size match,
SHA-256 match, and `GET /health → {"status":"ok"}`. Behavioral verification is
separate and light: a one-message probe over the shared chat template before a
live session (earlier zero-cost probes, e.g. arithmetic returning `323/stop`
through the runtime-adapter live test on the prior 12B runtime) plus the
user-perceived fact-check (§4.1) documenting the abliterated/base divergence
and *why* a one-model-at-a-time regime is enforced — swapping the variant is a
launcher restart, never a concurrent promotion.

## 5. The safety model

The threat model is brutally simple: the model is not trusted to be either
correct or safe. Every side effect is mediated:

- **Capability grants.** A workspace starts with no tool permissions. The first
  call of each capability (`READ`, `LIST`, `WRITE`, `EXEC`, `VERIFY`,
  `MCP_LAUNCH`, `MCP_CALL`) prompts; subsequent calls per-operation prompt
  unless explicitly pre-approved.
- **Writes are conservative.** `ft_write` is create-only; overwrite requires
  `ft_replace_approved` with the exact expected original bytes — a mis-read
  file cannot blind-overwrite a delta.
- **Execution is an allowlist, not a prompt.** `run_command` accepts only an
  exact absolute executable plus displayed argument tail, with repeated
  `--allow-exe` entries; shells and interpreters are categorically denied.
- **Verifier gating is the completion oracle.** A run is finished only when a
  caller-supplied verifier exits 0; everything else (timeouts, malformed
  output, unknown stops) fails the run. There is no *inferred* "task done."
- **Deny-by-default time-outs.** Approval replies time out in 120 s to *deny*;
  a stale or mismatched `request_id` never authorizes.
- **Unknown outcomes stop the run.** Cancellation of a tool mid-flight,
  verifier approval races, MCP timeouts: each is surfaced as UNKNOWN. The
  checkpoint becomes unsafe and the run stops rather than guessing.
- **Studio inherits all of it.** The JSONL protocol is the same surface;
  there is no separate "IDE permissions" concept, so patching the protocol is
  the same patch as patching the CLI.

What it does *not* do, stated plainly (§7): the tools library is not an OS
sandbox; allowlisted executables inherit the caller's token and environment;
and the C-threaded FS path has TOCTOU gaps (§7).

## 6. Integration surface

The server is deliberately the *boring* edge of FORGE: OpenAI-compatible
`/v1/chat/completions` on `http://127.0.0.1:18932/v1`, `GET /health` for
liveness, bound to loopback only. Consumers:

- **OpenCode** (`forge/qwen36-uncensored`, `forge/qwen36-local`) via
  `opencode.json` base URL `http://127.0.0.1:18932/v1`.
- **Any OpenAI-chat-completions client**: IDE forks, Aider-style
  `--openai-base-url`, or a 20-line script. The example prompt for driving an
  agent against this endpoint lives in
  `docs/installation/agent-prompt-example.md`.
- **Docker** via `http://host.docker.internal:18932/v1`; the server binds
  `127.0.0.1`, so it is never LAN-reachable by configuration accident.
- **The harness itself** consumes the same endpoint through the runtime
  adapter (WinHTTP loopback only).

The variant switch is a launcher run, not a model promotion:
`start-qwen-local.ps1 -ModelVariant Base` verifies the new GGUF, stops the
previous *verified llama-server* running on the port, and rebrands the port —
we refuse to stop foreign processes, so port-18932 contention by an unrelated
process fails loudly.

## 7. Limitations and disclosures

1. **The abliterated variant hallucinates more on factual recall than base**
   under the identical serving config (§4.1). Mitigation: the verifier gate
   (§5), preventing hallucinated "done" states from completing tasks.
2. **Not a sandbox.** Allowlisted executables inherit the caller's token and
   environment, meaning Windows-level mitigation for inspection races,
   TOCTOU on FS reads, and side channels binding doc results to untrusted
   markers remain (§5, documented in `tools/README.md`).
3. **Ram pressure with the server running** during heavy non-FORGE workloads
   (~1.6 GB free in one session) can choke CPU-offloaded experts; batch sizes
   at that level are user-visible slowdowns, correct behavior.
4. **Windows-only**, POSIX port absent. `Write`-side CLI/Studio parity is
   asymmetric until Studio ships.
5. **Runtime fork maintenance** is real work: the pinned upstream carries an
   upstream quirk (token-type override on `<eos>`/`<|tool_response>`/`</s>`)
   that depends on GGUF metadata alignment; drift is audited but not eliminated.

## 8. Conclusion

FORGE's contributions are not a new model: they are (a) an architecture where
the *harness*, not the model, is the trust boundary, (b) a measured serving
recipe for 35B-A3B MoE models on 16 GB consumer VRAM, with a documented
case study of the config error that produces a 26× slowdown when AI coding
feels slow, and (c) a checkpoint/verifier semantics that lets a local,
weakly-grounded smallish model participate in real code changes with
audit-grade insist-on-evidence gates. Local-first is difficult and it is
fragile in places this paper catalogs — but it is workable on one GPU already
owned, the contract layer absorbs the failure modes generalist stacks are
structurally unable to absorb, and the stack proposed here is open enough
that its own release history is part of the evidence. Democratized
intelligence is not a smaller copy of the cloud; it is a different contract
between three layers — model, runtime, harness — where every layer is one
that its user can read.

## References

1. Ggml contributors — *llama.cpp*, https://github.com/ggml-org/llama.cpp (MIT).
2. Antirez — *ds4*, a minimal C harness, https://github.com/antirez/ds4 (MIT).
3. Qwen team — *Qwen3.6* model card and license (weights not redistributed).
4. Alibaba — *Qwen fine-tuning/serving hints on MoE architectures* (serving
   background for §4).
5. Windows AI stack — *WinHTTP loopback-only client contracts* (§6).
6. FORGE — `docs/architecture/overview.md`, `docs/protocols/mcp.md`,
   `docs/protocols/parser-notes.md` (protocol-level references for §3).

