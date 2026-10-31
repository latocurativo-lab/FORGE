# FORGE Studio — execution log

**Data:** 2026-10-29  
**Esito corrente:** IN CORSO — non pronto; TaskFlow acceptance non avviata.  
**Regola:** registrare evidenze osservate, non intenzioni. Aggiornare dopo ogni fase; non sovrascrivere i report storici.

## Aggiornamento 2026-10-29 — transcript store v6

- Aggiunti comandi Tauri `load_sessions`/`save_sessions` e registrati nell'handler. Archivio snapshot JSON sotto `app_data_dir`, limite 64 MiB, validazione record; sostituzione su Windows usa file temporaneo e backup ripristinabile. Se il file è illeggibile o la migrazione fallisce, la sorgente originale non viene rimossa.
- La UI migra una tantum i record `localStorage` nello store nativo lasciando la copia browser intatta; scrive le modifiche in coda serializzata. Rimossi i tagli di 50 sessioni e 500 eventi.
- Non confondere questo intervento con transcript append-only o session resume: ogni invio continua ad avviare un processo nuovo e sessione/run coincidono nella UI; il backend non riceve i turni precedenti. Auto-memory, AGENTS/skill discovery e context compaction non implementati.
- Packaging riproducibile v6 completato: `artifacts/forge-studio-portable-v6/`. SHA-256 Studio `2C6DEA6CEB29345A4809A20FB16DAFED478C7C006F5A36FDC00E7C8168783AEB`, backend `11B0A37A8BEB95C91629FCFE684421F669B3EFEBBEF30D61CED3293081377E2B`.
- Test durante il packaging: CTest 9/9; `npm test` 9/9; `cargo test --locked` 6/6; build Tauri release PASS. Nessun task modello/agent, smoke UI v6, run TaskFlow o test di codice generato eseguito. Rimangono warning C4996 per `fopen` in `agent/src/agent_loop.c` e warning chunk Vite >500 kB.
- Verdetto: **PARTIAL**. Il bundle è utilizzabile per l'interfaccia corrente e conserva la cronologia, ma non soddisfa la richiesta di continuità reale, memoria o project instructions/skills; non va presentato come app finita.

## Baseline e checkout

- Workspace: `C:\Users\geekandhack\Desktop\FORGE`.
- Il checkout risulta dirty e contiene molti percorsi non tracciati e modificati prima di questa fase (`git status --short --branch`). Nessun reset/clean è stato eseguito.
- In questa attività sono stati aggiunti il piano `docs/plans/forge-studio-redesign-e2e-superplan.md`, questo log e `artifacts/forge-e2e-acceptance.md`. Modifiche di codice non esecutive in `studio/`: gestione recente progetti, ricerca e raggruppamento sessioni, rinomina/rimozione cronologia, tab UI, timeline centrale basata sugli eventi reali, composer centrale, viste conversazione/editor, label esplicite di Plan/runtime non supportati, watcher Vite escluso da `src-tauri/target`, visualizzazione dei path Windows senza prefisso esteso. Non è stata modificata la logica agente né lanciato `forge.exe` per un task.
- Il glob del workspace non ha trovato screenshot locali PNG/JPG/WebP. Le immagini fornite nella conversazione sono il riferimento dichiarato, ma non sono disponibili qui come file per un confronto pixel-by-pixel; non si afferma di aver completato tale ispezione.
- OpenCode verificato da ricerca indipendente: riferimento V2 desktop più chiaramente immutabile identificato `v2.2.0`, tag commit `1097e2668b1e08ad63bdf0b1c4860b9bc01e420c`; package `@opencode/desktop` e `@opencode/app` dichiarano MIT a quel commit. V1 `v1.19.5` è un release track distinto. Non copiare codice/asset finché non è completato l'audit delle licenze degli asset e delle dipendenze. Fonti: release/tag GitHub, manifests raw al commit e docs `/docs/` + `/docs/agents/`.

## Audit indipendenti (4 agenti)

1. **Frontend:** una schermata React/Tauri, `studio/src/main.tsx` concentra stato, workspace, editor, sessioni, approval e log. Monaco, dirty-state, workspace IO e bridge JSONL sono riutilizzabili. Mancano registro progetti, ricerca/rename/tab persistenti, store durevole, resume UI, Plan/Build, review con provenienza, PTY e runtime/activity telemetry.
2. **Backend:** JSONL espone start/cancel/approval e tipi evento quali `tool_request`, `tool_result`, `assistant_final`, `verification_*`, `error`, `exit`; non ha schema versionato né Plan policy. CLI `forge resume` esiste ma Studio non può riprendere; checkpoint unsafe non vanno replayati. Verifier è processo approvato con exit code, non verifier atomico dei requisiti. MCP richiede approval distinta di launch e call, ma Studio production E2E non è provato.
3. **Sicurezza:** `PAIT-GGUF-100` rimane irrisolto; il template verificato è un probe singolo e gli override dei token registrati richiedono confronto. Licenza modello è contraddittoria tra `models/MODEL.lock` (Apache-2.0 per repo Unsloth) e `LICENSES/NOTICE.md` (Gemma Terms of Use). Nessun sandbox runner o containment suite è presente nel checkout. Job Object/allowlist/temp dir non sono sandbox.
4. **OpenCode:** UX V2 e permessi di Plan pertinenti confermati; versione di riferimento fissata nel paragrafo sopra. Licenza root MIT non dimostra le licenze di ogni asset/dependency.

## Verifiche eseguite in questa fase

| Verifica | Esito osservato | Note |
|---|---|---|
| `npm test` in `studio/` | PASS 3/3 | Copre solo helper dirty-state; non UI/integration. |
| `npm run build` in `studio/` | PASS | TypeScript e Vite; warning bundle principale ~3.57 MB (gzip ~928 kB), da ottimizzare. |
| `cargo test --locked --manifest-path src-tauri/Cargo.toml` | PASS 2/2 | `cargo` non è nel PATH della shell; eseguito via `%USERPROFILE%\.cargo\bin\cargo.exe`, versione 1.99.1. |
| `ctest --test-dir build-agent-verify -C Release --output-on-failure` | PASS 9/9 | Suite di test esistente, non acceptance TaskFlow. |
| Aggiornamento session model: `npm test` | PASS 6/6 | Test di titolo, ricerca per titolo/task/workspace e gruppi temporali; non UI E2E. |
| Aggiornamento Tauri: `cargo test` | PASS 3/3 | Include conversione path Windows esteso/UNC per visualizzazione. |
| Build frontend dopo modifiche UI | PASS | Vite/TypeScript; bundle JS principale ~3.58 MB (gzip ~931 kB), warning >500 kB ancora aperto. |
| Build release Tauri | PASS | `npm run tauri -- build`; binario `studio/src-tauri/target/release/forge-studio.exe`, 12,082,688 byte, SHA-256 `AD9D61BF1E9462173D5AE64AB7EE102D2E3A7E1C6BF473A7AFDDFB3F060830B4`. |
| Avvio release | PASS, osservazione limitata | Processo `forge-studio.exe` attivo e rispondente. La schermata mostra cronologia/config persistenti locali preesistenti; questi dati non sono fixture né evidenza di acceptance. Non sono stati inviati nuovi task. |
| Fix discovery backend + stato UI | PASS limitato | Aggiunti comando di stato/selezione persistente, discovery di `build-agent/Release` solo nella root checkout riconosciuta e backend affiancato. Avvio del bundle v3 mostra `forge.exe` trovato nello stesso bundle. Nessun task agente inviato; non prova l'esecuzione JSONL del backend. |
| Sessioni stale al riavvio | PASS unitario | Il vecchio stato `running` viene recuperato come `interrupted`, senza dichiarare un resume o bloccare l'avvio di una nuova sessione. Test unitario aggiunto. |
| Packaging locale ripetibile | PASS | `scripts/package-forge-studio.ps1`: CMake in `build-agent-studio`, CTest, test UI/Rust, Tauri release, bundle `artifacts/forge-studio-portable-v3` con Studio, backend e manifest hash. Nessuna Gemma inclusa. |
| Hash bundle v3 | Registrati | Studio `A31665E4D6FD101232AC563AC980FE8896189C417BF5284B9D9AE11E2E3A6D53`; backend `4ADEE35572D4E8F556A815EF582ED15D848CD02B66087DCC3B008B649051E7EB`; manifest `artifacts/forge-studio-portable-v3/SHA256SUMS.json`. |
| Verifiche fresche su sorgente corrente | PASS | `npm test` 7/7; Cargo 4/4; CTest da `build-agent-studio` 9/9; `npm run build` e release Tauri riuscite. Warning chunk Vite >500 kB resta. |
| Runtime+agente da UI con Gemma | NON ESEGUITO | Richiede avvio modello/llama-server e avvia loop agente; lasciato al test d'uso richiesto dall'utente. Non è risultato di acceptance né test backend production. |
| Launcher `-Mode Studio` senza argomenti | FIX APPLICATO / WhatIf PASS | PowerShell rifiutava `Start-Process -ArgumentList` vuoto. Il launcher ora omette il parametro per l'avvio standard; `-WhatIf` conferma la risoluzione dei path senza hash/processi/rete. Lo smoke reale dell'apertura via launcher è lasciato al rilancio utente; il tentativo precedente ha arrestato correttamente solo il llama-server avviato dal launcher dopo l'errore. |
| Modalità Conversazione separata | PASS automatici, manuale da fare | Conversazione predefinita passa mode=chat, non offre tools/MCP e salta completamente verifier; coding preserva gate. Aggiunti test parser/state machine/UI. Non è stato chiesto al modello di rispondere durante questa build.
| Timeline conversazione | PASS build/test | Nel centro UI restano solo messaggi utente e `assistant_final`; `start`, `generation_started`, tool/verifier/exit restano in Attività laterale. Config backend/verifier/MCP collassata di default.
| Approval timeout | PASS build | Attesa backend da 120s a 30s; timeout produce `approval_timeout` e negazione. La UI mostra immediatamente la richiesta con indicazione di scadenza. Non è stato atteso un timeout reale end-to-end.
| Bundle portabile v4 | PASS build/launch | `artifacts/forge-studio-portable-v4`; avviato e rispondente, backend affiancato. Hash manifest incluso. Nessuna chiamata modello/task agente da questo run.
| Approval scaduta in UI | PASS unitario | `approval_timeout` chiude la card correlata usando `tool_call_id`; la card mostra Scaduta e non presenta più i pulsanti. Test helper aggiunto.
| Limite generazione conversazionale | PASS build | In chat `max_tokens=256`; Coding conserva il limite precedente 1024. Non implica garanzia di latenza massima del runtime.
| Test freschi dopo separazione chat | PASS | CTest 9/9, Studio 9/9, Rust 4/4; build frontend e Tauri Release.
| Bundle portabile v5 | PASS packaging/manifest/launch | `artifacts/forge-studio-portable-v5`; dopo il recovery della copia interrotta, Studio SHA-256 `D5F63AB929EED08C370BA791D06CFCFA0B295762EAB0C25EAED199F247A9ED6F`; backend `96BC580743BFC147F8CDAF4AA87FC7EF5E4F590A623B841B5C50F2FE4D67DC81`. Manifest aggiornato e verificato; firma PE `MZ` valida; app avviata, titolo FORGE Studio e processo rispondente. Nessun task Gemma lanciato.
| Avvio UI Tauri dev | PASS dopo fix watcher | Primo tentativo FAIL con Vite `EBUSY` su `src-tauri/target/debug/deps/forge_studio.exe`; dopo esclusione `**/src-tauri/target/**` il dev server e `forge-studio.exe` si sono avviati. |
| Smoke filesystem nativo UI | PASS limitato | In finestra Tauri dev: scelta workspace temporaneo `forge-studio-ui-smoke-20261029-01`, listing e apertura `notes.txt`, Ctrl+S, contenuto su disco verificato. Nessun agente/runtime/modello è stato avviato; non è smoke `forge.exe` production.
| Identificazione host Windows Sandbox | BLOCCATO / UNKNOWN | Windows 11 Pro build 26300; `WindowsSandbox.exe` non trovato nel PATH. Query DISM `Get-WindowsOptionalFeature -Online` richiede privilegi elevati e non è stata eseguita con elevazione. Non è prova che la feature sia disabilitata; non è disponibile prova del sandbox. |
| Hash binari preesistenti | Registrati | Studio `783BEDF4F70DFA481E468057BF1F92EFE5D1FE3E518299DA7C0CA6EE8C96BB84`; `build-agent-verify/Release/forge.exe` `C683F8F1D9AB76C9AB6448166A2BAD25D602A83B760E3B0BFC5D04A56E5ED657`. Sono hash della baseline, non certificazione della build finale. |

## Gate aperti e decisione corrente

- **G0 parziale:** baseline, audit componenti, design read e test correnti raccolti; l'UI è stata vista in Tauri dev e provata con file temporaneo; manca ispezione registrabile screenshot allegati e manifest completo versione/hash/dependency.
- **G1 non superato:** finding modello/licenza/template da chiarire; sandbox host non provata; containment runner assente. Non avviare TaskFlow agentico né eseguire build/test generati.
- **G2–G10 non completati/parziali:** UI iniziale di sessione/progetti implementata ma non completa; backend/UI persistenti, Plan enforcement, review/provenance, terminale PTY, metrica integrata, production native E2E, acceptance e release non eseguiti.
- Il task richiesto non viene simulato né marcato PASS/PARTIAL come risultato di prodotto: l'acceptance run non è iniziata. Se il blocco sicurezza resta, il verdetto finale non potrà essere PASS.

## Registro v8 (2026-10-29)

| Voce | Esito | Note |
|---|---|---|
| Parser continuita' C (`forge_parse_context_fields`) | PASS | CTest `test_context_fields`; cap 64 messaggi/32768 B per messaggio/256 KiB totali; istruzioni fino a 32768 B. |
| Replay cronologia nel loop agente | PASS | system -> cronologia -> task in `forge_run_agent`; fallback chat mode senza ricreare processi. |
| Ponte Rust: validazione history + memoria progetto | PASS | `cargo test` 8/8; `load_project_memory`/`save_project_memory` con backup e cap 16 KiB; `validate_history_json` testato. |
| AGENTS.md dal workspace | PASS | Letto dal backend come contesto non attendibile, mai permessi; indicatore UI. |
| Discovery skill | PASS automatici | Scansione `.forge/.agents/.claude/.opencode/skills`; id sicuri; frontmatter+body con cap 8 KiB per skill, 64 skill, selezione UI con checkbox; composito memoria+skill cap 32768 B; verifica workspace aperto = workspace chiamata. |
| Test frontend | PASS 12/12 | Nuovi `session-turns` (rebuild turni, cap, casi degeneri). |
| Bundle v8 | PASS packaging | `artifacts\forge-studio-portable-v8` con manifest SHA256: Studio `247CC652...09F5`, forge `EB8B8ED2...373`. Un primo packaging e' finito in `Desktop\artifacts` per errore di path relativo; corretto spostando il bundle nel repo. |
| Resume automatico sessione interrotta | NON IMPLEMENTATO | allo stato: cronologia salvata + niente richieste false; il resume resta noto come mancanza. |
| Compattazione contesto / acceptance TaskFlow | NON ESEGUITO | gate sicurezza/licenza/sandbox ancora aperti. |

## Registro gates pre-acceptance (2026-10-29)

- 2026-10-29 — audit read-only: prove per `PAIT-GGUF-100` (finding di classe GGUF-SSTI, template locale byte-identico al distributore, SHA-256 `845f1ee4…`, marker SSTI assenti) e licenza (Gemma 4 = Apache-2.0 da fonti primarie) in `artifacts/forge-gates-model-license.md` + `artifacts/model-license-evidence/`; design+skeleton rifiutante sandbox in `artifacts/forge-sandbox-design.md` + `scripts/sandbox/forge-sandbox-run.ps1`; checklist atomica TaskFlow + verifier esterno in `artifacts/forge-taskflow-acceptance-plan.md`; nessuna esecuzione di codice generato, nessun run, nessuna suite negativa svolta.

## Registro v9 (2026-10-29)

| Voce | Esito | Note |
|---|---|---|
| Resume sessioni interrotte | PASS test/build | Bottone esplicito Riprendi; replay solo turni completati nella stessa sessione logica. Test frontend coprono status interrupted, cap history, tool call pendenti escluse. Nessun checkpoint processo. |
| Compattazione contesto | PASS test/build | Digest estrattivo deterministico dei turni omessi, max 8 KiB, righe intere con contatore; memoria + digest cap 32 KiB; UI mostra numero turni riassunti. Nessun LLM di sintesi. |
| Verifica sorgente v9 | PASS | npm test 27/27; npm run build PASS; cargo test 8/8; CTest 10/10. |
| Bundle v9 | PASS | rtifacts\forge-studio-portable-v9; Studio SHA-256 9E21AC4926CA95AF0C53B4BF71AD26C18A3CA660BE4B9BCC5C2016AEA114C595; backend EB8B8ED25CC40D7C58A81D5DB15E4ACED8178FCD13CF9635A7994F255D7E7373; manifest nel bundle. |
| Gate documentale PAIT/licenza | Evidenze registrate, decisione aperta | orge-gates-model-license.md: template locale byte-identico al distributore; fonti Gemma 4 Apache-2.0. Restano non verificati audit Jinja C++, tokenizer completo e decisioni NOTICE/policy redistribuzione. |
| Sandbox / TaskFlow | NON ESEGUITI | Design, skeleton default-deny e piano TF-01..TF-16 documentati; skeleton non eseguito, verifier esterno non implementato, suite negativa non eseguita. Nessun codice/test generato lanciato. |

Verdetto: feature Studio resume+compaction passano i test locali; acceptance complessiva resta **PARTIAL/BLOCCATA** finche' decisioni licenza/policy e sandbox/verifier non vengono realmente chiuse.

## Diagnostica UI v10 (2026-10-29)

| Voce | Esito | Note |
|---|---|---|
| Diagnosi finestra v9 | PARZIALE | Processo Tauri risponde; non acquisita telemetria affidabile del WebView v9. Tentativo DevTools ha agganciato per errore OpenCode, non FORGE; nessuna conclusione dal suo console va attribuita a FORGE. Nessuna interazione GUI ulteriore. |
| Fallback boot/error boundary | PASS test/build | Aggiunti schermata boot, handler error/unhandled rejection, React ErrorBoundary con stack e reload. Frontend 27/27, npm run build PASS. |
| Bundle v10 | PASS packaging, NON smoke GUI | rtifacts\forge-studio-portable-v10; Studio SHA-256 51744C6435FE3DBB7CF608DBB152787365661D08B232AA91FE70964205F4F8CA; backend invariato. Non avviato: l'utente ha chiesto di evitare computer-use e non � stata fatta verifica visuale. Root cause del nero in v9 non accertata.

## Fix schermata nera confermato (v11, 2026-10-29)

- **Root cause riprodotta in v10:** React ErrorBoundary ha mostrato ReferenceError: Buffer is not defined in session-model.mjs, funzione che calcola la dimensione UTF-8. Buffer?.byteLength non e' safe in browser quando Buffer non e' dichiarato (lo short-circuit optional chaining non evita la risoluzione del nome).
- **Fix:** sostituito il fallback Node/browser con TextEncoder nel modulo condiviso; aggiunto test multibyte UTF-8. Il v10 aveva introdotto startup screen + window error handlers + React ErrorBoundary per rendere il problema visibile; nessun dato transcript modificato.
- **Test v11:** npm test 28/28; npm run build PASS; CTest 10/10; Rust 8/8; packaging PASS.
- **Smoke v11 read-only:** app avviata via launcher ufficiale dal bundle v11, PID 6852, Responding=True, health runtime HTTP 200. UI Automation tree del WebView ha enumerato i controlli applicativi attesi (FORGE STUDIO, Apri cartella, PROGETTI, file, SESSIONI, composer, Invia); nessun testo di startup/error-boundary. Nessun computer-use e nessuna interazione/cambio dati.
- Bundle rtifacts\forge-studio-portable-v11; Studio SHA-256 20C1E176B1C237B292AA2EC6A2EDF863CA2D816C2AD02986302A544486BC5437; backend invariato EB8B8ED25CC40D7C58A81D5DB15E4ACED8178FCD13CF9635A7994F255D7E7373.

## Fix continuit� turno / slot agente (v13, 2026-10-29)

- **Root cause crash backend:** in gent/src/main.c, se checkpoint mancava, veniva assegnato a generated_checkpoint (array sullo stack), poi ree(checkpoint) lo liberava come heap. Application Error del v11:  xC0000374 (heap corruption); smoke riprodotto prima del fix, codice di uscita 3221226356.
- **Fix ownership:** checkpoint_is_generated evita di liberare l'array locale; checkpoint allocato dal parser continua a essere liberato correttamente.
- **Shutdown stdin:** bridge Rust chiude il pipe stdin al terminal event logico, cos� cancel_reader riceve EOF. Il backend aspetta il reader normalmente per 5s e usa CancelSynchronousIo solo come fallback di emergenza. Il test MCP � aggiornato per chiudere stdin al terminal event.
- **Stato frontend:** un evento exit C senza code (es. messaggio COMPLETED) non marca pi� erroneamente la sessione come error; aspetta il vero exit code del processo Rust.
- **Test:** CTest 10/10; frontend 28/28; Rust 8/8; build frontend/Tauri PASS. Smoke diretto con protocollo JSONL tenendo stdin aperto fino al terminal event: due chat sequenziali (OK, CIAO), entrambe risposta ricevuta, exit code 0, stderr vuoto. Nessun codice/test generato eseguito.
- **Smoke finestra v13 via UI Automation read-only:** Studio Responding=True, controlli Apri cartella, PROGETTI, SESSIONI, Invia presenti; nessun boot-error; launcher runtime health 200. Nessuna interazione di input.
- Bundle attualmente aperto: rtifacts\forge-studio-portable-v13; Studio SHA-256 F9B8569642303221254541F502C57B4AB91DB46287257FB6B7DA7650264D0650; backend 7036D5B633CDFDA880A2F36DD7722756DF4E051DC1D2685B8B227DE781E44E0C.
