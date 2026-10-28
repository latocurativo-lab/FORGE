# FORGE — Notice licenze

Aggiornato: 2026-10-28 (M0)

## Componenti del progetto

### 1. Gemma 4 12B-it (modelli GGUF)
**Gemma license — verifica Fase 1.**

Il modello è distribuito con la licenza proprietaria di Google ("Gemma Terms of Use").
NON è Apache 2.0 puro. Il repo originale `google/gemma-4-12B-it` su Hugging Face è
gated (richiede accettazione dei termini). Punti da verificare in Fase 1 (M0/M1):

- [ ] Termini d'uso aggiornati dei modelli Gemma 4 (restrizioni su redistribuzione
  dei pesi quantizzati e su output del modello).
- [ ] Se una derivata quantizzata (Q4_K_M) prodotta da terzi su HF è ridistribuibile
  sotto i medesimi termini o richiede l'accettazione esplicita della licenza gated.
- [ ] Uso commerciale interno senza ridistribuzione dei pesi: confermare che sia
  coperto; in caso di ridistribuzione del bundle FORGE+modello, rifiutare e
  lasciare il download del modello all'utente finale.

**Non scrivere/affermare che il modello sia Apache 2.0.** Il codice runtime sotto è
MIT: modello e runtime hanno licenze distinte.

### 2. llama.cpp (runtime forkato)
**MIT** — https://github.com/ggml-org/llama.cpp/blob/master/LICENSE.
Il fork mantiene la licenza e i copyright originali; le patch in `runtime/patches/`
seguono la licenza del progetto.

### 3. ds4 (riferimento di design per l'harness)
**MIT** — https://github.com/antirez/ds4. Usato come riferimento stilistico/
architetturale per l'harness in C: non è incluso codice copiato; se in futuro
verrà incorporato codice, portare l'header di licenza MIT originale.

### 4. Tauri, React, Monaco (Studio, da M3 in poi)
Tauri: MIT/Apache-2.0. React: MIT. Monaco Editor: MIT.
Da riportare in Fase 6 (Studio) con le versioni esatte.

## Cosa NON è incluso
- CUDA Toolkit / driver NVIDIA: licenze proprietarie NVIDIA, il progetto non li
  ridistribuisce; le build CUDA sono per uso locale sul macchine dell'utente.
