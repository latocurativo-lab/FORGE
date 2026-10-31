# FORGE M0 — stato verificato

**Stato: non dichiarata pronta.** Il prodotto è compilabile e i gate funzionali
principali hanno evidenza, ma restano limiti di sicurezza e verifiche native
end-to-end da chiudere.

## Evidenze completate

- Runtime: llama.cpp locale, commit `522021325546ea322b0b1e58a50f9dbf03a5f197`;
  modello Gemma 4 12B-it Q4_K_M, 7.121.861.440 byte. SHA-256 locale
  `0a270ec9fe6b34f4a0d33992b6135117b484ebc4766ab76b51d4ae8c457e4c42`, uguale
  all'OID LFS del distributore; questo prova integrità del file rispetto alla
  fonte, non correttezza dei pesi o sicurezza del template.
- Benchmark popolato completato: 4K/8K/16K/32K con 3 repliche ciascuno, 64K e
  128K con una replica esplorativa. Massima richiesta osservata: 131.073 token
  `usage.prompt_tokens` al target 128K; 5.086 MiB minimo libero nel campionamento
  globale. Report e log: `artifacts/bench/context-run-20261029.md`.
- Agent C11 + runtime adapter + tools compilano con MSVC; CTest corrente: 9/9,
  inclusa nuova prova integrata agente → server MCP fake → approvazioni distinte
  launch/call/verifier. Client MCP autonomo MSVC/fake server: 1/1. Cancellation:
  generazione, attesa approval e tool/verifier process tree coperte dai test,
  con checkpoint unsafe e nessun replay automatico.
- Tauri Windows ricompilato: `studio/src-tauri/target/release/forge-studio.exe`.
  `cargo check --locked`, Rust test 2/2, frontend test 3/3, build TypeScript/Vite
  e `npm run tauri -- build` passano con configurazione MCP. Lo smoke UI nativo
  precedente usava un fixture bridge, non il backend `forge.exe` di produzione.
- Live Gemma E2E precedente: due scritture approvate e verifier nativo separato
  con esito 0. Il verifier controllava marker strutturali, non eseguiva i test
  JavaScript generati. Nessuna affermazione che `node:test` sia passato.

## Limiti aperti

1. Protect AI segnala `PAIT-GGUF-100` sul GGUF. Non è prova di compromissione,
   ma tokenizer/template, override dei token di controllo e runtime richiedono
   audit indipendente prima di uso agentico con strumenti.
2. I test JavaScript generati non vengono eseguiti: non è ancora progettato un
   runner esplicitamente autorizzato e confinato. I tool Windows non sono una
   sandbox; gli eseguibili approvati ereditano token, ambiente e rete.
3. Lo smoke test del bridge Tauri ha usato il fixture C; non è stato eseguito
   uno smoke UI completo con l'eseguibile FORGE prodotto nel build corrente.
4. 64K/128K sono pass singoli esplorativi, non misure di ripetibilità. Il probe
   genera solo 16 token (`finish_reason=length`) e non valuta qualità/accuratezza.
   VRAM e utilizzo sono telemetria globale WDDM, campionata ogni 500 ms.
5. Restano da verificare autonomamente i termini della licenza del modello
   d'origine. Non è stato fatto alcun commit.

## Aggiornamento FORGE Studio — 2026-10-29

- Risolto il problema di discovery `forge.exe`: il bundle locale affianca il backend e Studio mostra path/origine, con selezione persistente del backend. Bundle verificato: `artifacts/forge-studio-portable-v3/`.
- UI ridisegnata con gestione progetti/sessioni, timeline e editor; sessioni residue `running` dopo riavvio vengono marcate `interrupted`, non riprese.
- Build corrente: CTest 9/9, Studio 7/7, Rust 4/4; build release Tauri riuscita. Avvio bundle e rilevazione del backend verificati senza inviare un task agente.
- **Non equivale** a smoke JSONL production dalla UI o acceptance Gemma. Il test manuale d'uso è lasciato all'utente; per runtime usare `scripts/launch/forge-local.ps1 -Mode Studio -StudioPath <forge-studio.exe bundle>`.
- Restano invariati i blocchi: audit `PAIT-GGUF-100`/licenza/template, sandbox e negative containment, persistenza transazionale/resume, Plan backend, provenance, MCP nativo production E2E e TaskFlow acceptance. Il prodotto non viene dichiarato pronto per esecuzione agentica non confinata.
- Aggiornamento Studio: bundle v5 `artifacts/forge-studio-portable-v5/` aggiunge modalità Conversazione senza tool/verifier, modalità Coding separata, timeline conversazionale ridotta a user+assistant, generazione chat max 256 token e timeout approval a 30s con scadenza visibile e non riapprovabile. Build: CTest 9/9, frontend 9/9, Rust 4/4. Nessun test conversazionale Gemma è stato eseguito; il comportamento effettivo resta da confermare nel test manuale dell'utente.
