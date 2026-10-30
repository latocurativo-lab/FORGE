# FORGE — Notice licenze

Aggiornato: 2026-10-31

## Componenti del progetto

### 1. Qwen3.6-35B-A3B (modelli GGUF in `models/`)
Le varianti di FORGE sono quantizzazioni derivate: **Base** (`UD-IQ3_XXS`) e
**Abliterated** (`Uncensored-HauhauCS-Aggressive-IQ3_M`, edit comunitario con
rimozione aggressiva dei refusal). La famiglia Qwen è distribuita sotto licenza
**Apache-2.0** (con i notice Qwen); le GGUF derivate di terzi ereditano i termini
delle rispettive fonti (QuantTrio/Unsloth per la base, edit comunitario per la
variante abliterated).

FORGE **non ridistribuisce i pesi**: il repo contiene solo dimensioni + SHA-256
di riferimento (verificati dal launcher prima di ogni avvio), e il download resta
responsabilità dell'utente finale.

- [x] Base modellata su fonti pubbliche con SHA-256 verificato dal launcher:
      `9c964e65…b88fbe` (base), `4dd9a27c…be0c0` (abliterated).
- [x] Divulgato nel README/paper che la variante abliterated allucina di più
      sui compiti factuali rispetto alla base a parità di condizioni di serving.
- [ ] Uso commerciale interno: confermare i termini delle singole fonti GGUF;
      la licenza del codice del runtime (MIT) è indipendente dai pesi.

**Non affermare mai che i pesi del modello siano coperti dalla licenza MIT del
codice.** Pesi e codice hanno licenze separate: codice MIT, pesi secondo le
fonti upstream (Apache-2.0/termini derivate).

### 2. llama.cpp (runtime forkato)
**MIT** — https://github.com/ggml-org/llama.cpp/blob/master/LICENSE.
Il fork mantiene licenza e copyright originali; le patch in `runtime/patches/`
seguono la licenza del progetto.

### 3. ds4 (riferimento di design per l'harness)
**MIT** — https://github.com/antirez/ds4. Usato come riferimento stilistico/
architetturale per l'harness in C: non è incluso codice copiato; se in futuro
verrà incorporato codice, portare l'header di licenza MIT originale.

### 4. Tauri, React, Monaco (Studio)
Tauri: MIT/Apache-2.0. React: MIT. Monaco Editor: MIT. Da riportare in Fase 6
(Studio) con le versioni esatte.

### 5. Codice FORGE (harness, adapter, tools, MCP, launcher)
**MIT** — file `LICENSE` alla radice del repo.

## Cosa NON è incluso
- CUDA Toolkit / driver NVIDIA: licenze proprietarie NVIDIA, il progetto non li
  ridistribuisce; le build CUDA sono per uso locale sulle macchine dell'utente.
