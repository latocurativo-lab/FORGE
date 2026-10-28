# FORGE

Ambiente di coding agentico **100% locale** su Windows 11: il modello gira in GPU
(RTX 4060 Ti 16 GB), nessun servizio cloud, nessun dato che esce dalla macchina.

## Idea centrale

Un harness agentico scritto in C (stile ds4 di antirez) che guida un Gemma 4 12B-it
quantizzato Q4_K_M attraverso llama.cpp forkato, con tool-calling nativo del modello,
verifica delle completions e checkpoint/ripresa dei task. Studio (IDE Tauri+React+Monaco)
arriva solo dopo il traguardo headless M2.

## Stack deciso

| Componente | Scelta |
|---|---|
| Modello | Gemma 4 12B-it, GGUF Q4_K_M, thinking mode OFF di default (flag opzionale) |
| Runtime | Fork immediato di llama.cpp (`runtime/upstream` pinnato a commit + `runtime/patches`) |
| Agent/harness | C, stile ds4 (https://github.com/antirez/ds4, MIT) |
| Studio | Tauri + React + Monaco, solo dopo M2 |

## Roadmap M0-M6

- **M0** — Audit ambiente, fork llama.cpp, build CUDA, download modello, prima
  generazione misurata (tok/s, picco VRAM, KV cache a 16K/32K context).
- **M1** — Protocollo tool-calling Gemma 4 nel fork; stop reason disambiguate
  (fine turno ≠ fine messaggio ≠ richiesta tool ≠ troncamento).
- **M2** — FORGE headless CLI: state machine + completion verifier +
  checkpoint/ripresa. Go/no-go: task multifile con 20-30 tool call senza "continua".
- **M3** — Strumenti completi (file system, shell, search) con sandboxing.
- **M4** — MCP client/server.
- **M5** — Context engine (progetto+sessione+memoria lunga).
- **M6** — Studio IDE + packaging/portabilità.

## Struttura repo

```
agent/            harness in C (Agente C)
runtime/upstream/ llama.cpp pinnato (Agente B)
runtime/patches/  patch sul fork
models/           GGUF (~8 GB, ignorato da git)
docs/             architecture/, protocols/, benchmarks/
artifacts/        audit, report di build e benchmark
scripts/          setup/, build/, bench/
LICENSES/         notice licenze
```

## Compilazione e avvio (placeholder)

M0 non ancora completato: la procedura definitiva arriva dopo la build CUDA del fork.

```powershell
# pianificato (placeholder):
# 1. git clone / fork llama.cpp in runtime/upstream (pin a commit)
# 2. cmake -B build -DGGML_CUDA=ON
# 3. cmake --build build --config Release
# 4. models/ contiene gemma-4-12b-it Q4_K_M GGUF
# 5. .\build\...\llama-cli -m models\... -p "..."   # prima generazione misurata
# 6. agent\forge.exe  # harness headless (da M2)
```

## Stato

M0 in corso — audit ambiente: `artifacts/audit.md`.
