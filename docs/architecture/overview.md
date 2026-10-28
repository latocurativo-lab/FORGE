# FORGE — Architettura overview

Data: 2026-10-28 (M0). Stato: scelte chiuse di alto livello, dettaglio per milestone.

## I 4 componenti

```
┌─────────────────────────────────────────────────────┐
│                    Studio (IDE)                      │
│            Tauri + React + Monaco (M5+)              │
└──────────────────────┬──────────────────────────────┘
                       │ (RPC/IPC, solo dopo M2)
┌──────────────────────▼──────────────────────────────┐
│                  Agent (harness)                     │
│     C, state machine, tool loop, verifier,           │
│     checkpoint/ripresa, sandboxing (M2+)             │
└──────────────────────┬──────────────────────────────┘
                       │ (HTTP server llama.cpp / libreria)
┌──────────────────────▼──────────────────────────────┐
│                  Core (runtime)                      │
│  fork llama.cpp, CUDA, KV cache, protocollo          │
│  tool-calling Gemma 4, stop-reason disambiguate (M1) │
└──────────────────────┬──────────────────────────────┘
┌──────────────────────▼──────────────────────────────┐
│                Context (engine)                      │
│  progetto+sessione+memoria, prompt assembly,         │
│  (M4+, dopo MCP)                                     │
└─────────────────────────────────────────────────────┘
```

### Core (runtime)
- **Fork immediato di llama.cpp**: `runtime/upstream` pinnato a un commit scelto,
  modifiche solo in `runtime/patches` (serie applicabili con `git am`).
- Build CUDA (`-DGGML_CUDA=ON`) target RTX 4060 Ti 16 GB.
- Responsabilità M1: protocollo tool-calling Gemma 4 in cima al tokenizzatore/
  detokenizzatore e assiemi prompt (`<|tool>declaration:...<tool|>`,
  `<|tool_call>call:nome{arg:<|"|>val<|"|>}<tool_call|>` ecc.), e
  **stop reason disambiguate**: fine turno ≠ fine messaggio ≠ richiesta tool ≠
  troncamento per lunghezza.
- Q4_K_M GGUF; thinking mode OFF di default (flag `--thinking` per attivarlo).

### Agent (harness)
- **C**, stile ds4 di antirez: semplice, esplicito, pochi file, niente magia.
- Loop: modello → domanda se serve un tool → esecuzione tool sandboxed →
  `<|tool_response>` → continua.
- Completion verifier: ciò che considera "finito" deve essere provato (stato
  dichiarato vs stato del file system); checkpoint del task e ripresa dopo
  interruzione.
- Go/no-go M2: task multifile con 20-30 tool call senza intervento umano.

### Context (engine)
- Da M4+; su cui l'harness piazza assembly del prompt: file di progetto,
  sessione, memoria lunga, compressione contesto a 16K/32K.

### Studio (IDE)
- Da M5+; Tauri + React + Monaco. Solo DOPO il traguardo headless M2. Prima
  l'agente funziona da CLI; l'IDE è solo una vista sopra.

## Vincoli hardware (design)
- RTX 4060 Ti 16 GB VRAM, 32 GB RAM, SSD locale.
- Gemma 4 12B-it Q4_K_M ≈ 7-8 GB pesi + KV cache: budget VRAM richiede Misura
  a 16K/32K context (M0 produce le prime misure reali).
