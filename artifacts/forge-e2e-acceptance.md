# FORGE E2E acceptance — preflight report

**Verdetto corrente: PARTIAL / BLOCCATO PRIMA DEL RUN.**  
Questo è un report di preflight, non l'esito della prova TaskFlow. Il task principale, il test di continuità e quello di false conclusioni **non sono stati avviati**. Non è stata inviata alcuna richiesta a Gemma per TaskFlow e non sono stati eseguiti output di codice generato.

## Versioni e configurazione note

- OS osservato: Windows 11 Pro, build 26300.
- Studio sorgente: `studio/package.json` versione `0.1.0`; Tauri 2, React 19, Monaco `^0.52.2`, Vite `6.4.5` risolto nella build locale.
- Rust/Cargo test: Cargo `1.99.1` eseguito da `%USERPROFILE%\.cargo\bin\cargo.exe` (non presente nel PATH di questa shell).
- Build binari preesistenti al run: `studio/src-tauri/target/release/forge-studio.exe`, SHA-256 `783BEDF4F70DFA481E468057BF1F92EFE5D1FE3E518299DA7C0CA6EE8C96BB84`; `build-agent-verify/Release/forge.exe`, SHA-256 `C683F8F1D9AB76C9AB6448166A2BAD25D602A83B760E3B0BFC5D04A56E5ED657`. Hash baseline, non build finale.
- Nuova build parziale Tauri del 2026-10-29: `studio/src-tauri/target/release/forge-studio.exe`, 12,082,688 byte, SHA-256 `AD9D61BF1E9462173D5AE64AB7EE102D2E3A7E1C6BF473A7AFDDFB3F060830B4`; avviata e rispondente. L'app ha restaurato dati locali preesistenti, non usati come prove acceptance.
- Bundle portabile locale v3: `artifacts/forge-studio-portable-v3/`. `forge-studio.exe` SHA-256 `A31665E4D6FD101232AC563AC980FE8896189C417BF5284B9D9AE11E2E3A6D53`; `forge.exe` SHA-256 `4ADEE35572D4E8F556A815EF582ED15D848CD02B66087DCC3B008B649051E7EB`. La UI avviata dal bundle mostra `forge.exe` trovato accanto all'app. Il backend non è stato avviato via UI e nessun prompt è stato inviato.
- Bundle portabile locale v5: `artifacts/forge-studio-portable-v5/`; include selezione Conversazione/Coding, rendering conversazionale senza eventi tecnici, generazione chat limitata a 256 token e timeout approval 30s con card scaduta disabilitata. Manifest SHA-256 presente. Test Gemma, conversazione reale e coding reali NON ESEGUITI in questa sessione; sono lasciati all'utente.
- Modello registrato nei report precedenti: Gemma 4 12B-it Q4_K_M, file 7,121,861,440 byte, SHA-256 `0a270ec9fe6b34f4a0d33992b6135117b484ebc4766ab76b51d4ae8c457e4c42`. Non è stato caricato/verificato durante questo preflight.
- Hardware riportato da `artifacts/runtime-validation.md` (misurazione precedente, non verificata in questo run): RTX 4060 Ti 16 GB, driver 617.14.
- Provider cloud: non usato per il task; nessun task è stato inoltrato a un modello.

## Gate e test

| Gate/test | Esito preflight | Evidenza / limite |
|---|---|---|
| Workspace isolato, backend production e runtime locale | NON ESEGUITO | Nessun workspace TaskFlow o processo runtime avviato. |
| `PAIT-GGUF-100` | BLOCCATO | `artifacts/runtime-validation.md` documenta il finding ma non ne stabilisce significato/applicabilità; confronto template/tokenizer incompleto. |
| Licenza modello | BLOCCATO | `models/MODEL.lock` riporta Apache-2.0 per il repo di derivata; `LICENSES/NOTICE.md` dichiara applicabili i Gemma Terms of Use e lascia la redistribuzione da verificare. Non assumere Apache-2.0 per i pesi. |
| Sandbox generazione/test | BLOCCATO | Nessun runner/contenimento dimostrato nel repository. `WindowsSandbox.exe` non è stato trovato; verifica DISM dell'optional feature richiede elevazione e non è stata eseguita. Disponibilità host = sconosciuta, non “disabilitata”. |
| Studio frontend | PASS 9/9 test | `npm test` in `studio/`: include titoli, ricerca/raggruppamento, ripristino `running`→`interrupted`, filtro dei soli messaggi assistant e scadenza approval nel centro; nessuna UI E2E. |
| Frontend build | PASS | `npm run build`; bundle entry ~3.58 MB (gzip ~931 kB), warning chunk >500 kB aperto. |
| Tauri Rust | PASS 4/4 test | `cargo test --locked --manifest-path src-tauri/Cargo.toml`, incluso discovery path backend e display path Windows. |
| Agent CTest | PASS 9/9 fresco | `ctest --test-dir build-agent-studio -C Release --output-on-failure` include parser mode e transizione chat; non equivale a TaskFlow acceptance. |
| Avvio Tauri dev e smoke file UI | PASS limitato | App nativa avviata dopo fix watcher; workspace temporaneo, listing, read/editor e Ctrl+S verificati su disco. Non avviato l'agente. Primo avvio aveva fallito Vite `EBUSY` sul target; escluso `src-tauri/target/**` dal watcher. |
| Smoke UI backend FORGE production | NON ESEGUITO | L'evidenza storica in `artifacts/m0-status.md` era fixture-based; lo smoke corrente copre solo il bridge filesystem Tauri, non `forge.exe`. |
| Resolver `forge.exe` nel bundle v3 | PASS limitato | Launch nativo mostra status disponibile e path `artifacts/forge-studio-portable-v3/forge.exe`; nessun processo agente è stato lanciato. |
| Modalità chat evita verifier | TEST UNITARI + ISPEZIONE, E2E NON ESEGUITO | `chat_mode` disabilita schema tool/MCP e salta `verify()` dopo la risposta. La prova con llama-server/UI è deliberatamente lasciata all'utente.
| Approval scade senza attesa silenziosa di 120s | TEST BUILD, TIMEOUT REALE NON ESEGUITO | Il backend aspetta 30s e poi nega; nessuna simulazione temporale di end-to-end.
| MCP su UI nativa production | NON ESEGUITO | Evidenza fake-server/backend separata già esistente; non prova il flusso nativo end-to-end. |
| TaskFlow / continuità / false finali | NON ESEGUITI | Bloccati da sicurezza/sandbox e prerequisiti di integrazione. |

## Evidenze correnti e interventi

- **Aggiornamento 2026-10-29, bundle v6:** `artifacts/forge-studio-portable-v6/`. SHA-256 manifest: Studio `2C6DEA6CEB29345A4809A20FB16DAFED478C7C006F5A36FDC00E7C8168783AEB`; backend `11B0A37A8BEB95C91629FCFE684421F669B3EFEBBEF30D61CED3293081377E2B`. Test eseguiti durante il packaging: CTest 9/9, frontend 9/9, Rust 6/6, build Tauri release PASS.
- Il v6 aggiunge persistenza transcript nativa come snapshot JSON con migrazione non distruttiva dal localStorage e rimuove i tagli UI a 50 sessioni/500 eventi. Questo è **persistenza della cronologia**, non resume/context continuity: resta un processo nuovo per invio e il modello riceve solo il prompt corrente. Non è stata fatta una prova UI nativa del v6 né una sessione col modello.

- Quattro audit in sola lettura hanno esaminato frontend, backend/JSONL, sicurezza/sandbox e release/licenze OpenCode. Nessuno ha modificato il codice o eseguito output generato.
- In questa fase sono state eseguite solo le suite esistenti del progetto e la build web, non un acceptance task. Gli esiti sono registrati in `artifacts/forge-studio-execution-log.md`.
- UI non esecutiva aggiunta: registry progetti in localStorage, ricerca, rimozione solo dall'elenco, ricerca/raggruppamento/rename/delete sessioni, tab, timeline centrale, composer e switch editor/conversazione. Plan e telemetria sono etichettati non disponibili/non verificati; non è enforcement backend.
- Prompt di continuazione per TaskFlow: **0**, perché TaskFlow non è stato avviato. Interventi human approval durante il task: **non applicabile**. Recovery: **non applicabile**.
- Non sono disponibili metriche di turni, tool call, file TaskFlow, token o VRAM per questa acceptance: task non eseguito e fonti runtime non collegate.

## Problemi e requisiti non soddisfatti

1. Auditoria del finding `PAIT-GGUF-100`, template/tokenizer e licenza modello non conclusi.
2. Non esiste prova di un sandbox adeguato; codice e test generati non vanno eseguiti.
3. Studio v6 ha uno store nativo snapshot per la cronologia, ma non transcript append-only/SQLite, resume o continuità del contesto modello; non ha enforcement backend Plan. Smoke del backend production dalla UI ancora da fare.
4. Terminale PTY, review provenance, runtime telemetry integrata e verifier atomico per ogni requisito non sono verificati/implementati.
5. Screenshot allegati non sono disponibili come file nel workspace per l'ispezione visuale registrabile.

## Decisione

Il preflight non autorizza il task agentico con strumenti né l'esecuzione dei test generati. Lo stato è **PARTIAL/BLOCCATO**, non PASS. Riprendere il run solo dopo audit modello/licenza e sandbox con suite negativa di contenimento superati, poi collegare Studio a `forge.exe` production e aggiornare questo report con prove del run. Non dichiarare FORGE pronta all'uso quotidiano in questo stato.

- **Aggiornamento 2026-10-29, bundle v8:** `artifacts\forge-studio-portable-v8\`. SHA-256 manifest: Studio `247CC652681815DB9189908AA144D4171BF79B95C13610221523B65AFDDF09F5`; backend `EB8B8ED25CC40D7C58A81D5DB15E4ACED8178FCD13CF9635A7994F255D7E7373`. Test durante il packaging: CTest 10/10 (nuovo `test_context_fields`), Rust 8/8 (nuovo test skill), frontend 12/12 (nuovi `session-turns`), build Tauri release PASS.
- Il v8 aggiunge: continuita' intra-sessione (follow-up nella stessa conversazione via replay cronologia validata), memoria di progetto persistente, AGENTS.md caricato come contesto non attendibile, discovery skill in `.forge/.agents/.claude/.opencode/skills` con selezione UI e caricamento on-demand composito (cap 32 KiB). Tutto il contesto resta non attendibile e senza permessi.
- **Non ancora coperto:** resume automatico di sessione interrotta (resta cronologia salvata senza continuare il processo), compattazione del contesto, gate `PAIT-GGUF-100`/licenza, sandbox per esecuzione di codice/test generati, acceptance TaskFlow. Verdetto resta **PARTIAL**, non PASS.

- **Aggiornamento v9 (2026-10-29):** test di release CTest 10/10, frontend 27/27, Rust 8/8, npm run build PASS. Bundle rtifacts\forge-studio-portable-v9, Studio SHA-256 9E21AC4926CA95AF0C53B4BF71AD26C18A3CA660BE4B9BCC5C2016AEA114C595, backend EB8B8ED25CC40D7C58A81D5DB15E4ACED8178FCD13CF9635A7994F255D7E7373. Resume esplicito via bottone e digest estrattivo deterministico sono implementati e coperti da test. Il resume non ricostruisce tool call a met�; il digest non � sintesi LLM. Acceptance TaskFlow NON eseguita: sandbox/negative suite e verifier esterno ancora aperti. Verdetto **PARTIAL/BLOCCATO**.

- **Bug follow-up agente corretto in v13:** il backend Studio liberava con ree() un checkpoint allocato sullo stack (heap corruption  xC0000374 a fine risposta); il bridge chiude ora stdin al terminal event e UI aspetta l'exit code reale. Verifica: CTest 10/10; test frontend 28/28; Rust 8/8; due run chat sequenziali con exit 0; UIA read-only conferma controlli renderizzati; runtime health 200. Bundle aperto rtifacts\forge-studio-portable-v13, hash in SHA256SUMS.json. Nessuna interazione computer-use. Questa verifica � smoke non generativo, non l'acceptance TaskFlow.
