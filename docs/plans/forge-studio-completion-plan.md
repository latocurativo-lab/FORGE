# FORGE Studio — piano di completamento verificabile

**Stato:** PARTIAL — bundle portabile v6 creato con archivio nativo persistente della cronologia e migrazione conservativa. Non è ancora la release completa richiesta: contesto multi-turno/resume, memoria, discovery instructions/skills e acceptance native non sono conclusi. I gate modello/licenza/sandbox restano bloccanti per l'esecuzione di codice/test generati.

> **Roadmap completa e fonte di verità:** [`forge-studio-redesign-e2e-superplan.md`](forge-studio-redesign-e2e-superplan.md). Questo documento conserva il registro delle correzioni e build v3–v5; il toggle Conversazione/Coding della v5 è un workaround temporaneo, non il comportamento obiettivo. Il percorso target è una sola chat con intent routing, skill/MCP lazy, verifier condizionale, persistenza e gate di sicurezza.

## Avanzamento effettivo di questa esecuzione

- **Completato:** resolver Tauri ora cerca configurazione persistente, `FORGE_EXE`, backend affiancato/risorse, build `build-agent/Release` solo quando riconosce la radice del checkout, quindi PATH. Non sceglie `build-agent-verify`. UI mostra disponibilità, path e origine, e consente di selezionare `forge.exe` da file picker.
- **Completato:** sessioni salvate che risultano `running` al riavvio vengono marcate `interrupted`, non bloccano il composer come processi vivi e non fingono resume.
- **Completato in codice, non ancora acceptance manuale:** modalità Conversazione distinta da Coding attraverso UI, JSONL e backend. Conversazione invia prompt senza tool schema, salta MCP/verifier, termina con transizione dedicata `CHAT_DONE`; Coding continua a richiedere verifier/allowlist. Prompt conversazionale diretto e compatto.
- **Completato in codice, non ancora acceptance manuale:** il flusso centrale renderizza solo input e `assistant_final`, nascondendo eventi runtime; dettagli e approval sono nel pannello laterale, con configurazione progressiva collassata.
- **Completato in codice:** attesa approval ridotta da 120 a 30 secondi, con evento esplicito di scadenza e deny fail-closed.
- **Completato:** script `scripts/package-forge-studio.ps1` costruisce FORGE da directory CMake separata, esegue CTest + test frontend/Rust + build Tauri e crea un bundle affiancato con hash, senza sovrascrivere output esistenti.
- **Build consegnata:** `artifacts/forge-studio-portable-v3/` — Studio e backend sono entrambi presenti. L'avvio della UI ha mostrato il backend risolto nel bundle; non è stato inviato un task all'agente né avviato un modello.
- **Verifiche automatiche aggiornate:** CTest 9/9, Studio 9/9, Rust 4/4; build frontend/Tauri riuscite. Warning bundle JS (~3.58 MB minificato) resta aperto.
- **Nuovo bundle per test d'uso:** `artifacts/forge-studio-portable-v5/`, contenente il flusso Conversazione/Coding separato, risposte limitate a 256 token in chat e approval a 30s con stato scaduto disabilitato. Nessun task Gemma è stato mandato da questa sessione.
- **Nuovo bundle v6:** `artifacts/forge-studio-portable-v6/`, compilato con Tauri e backend di produzione; aggiunge `load_sessions`/`save_sessions` su storage dati Tauri con validazione schema/64 MiB, scrittura sostitutiva con backup/restore, migrazione iniziale dalla copia localStorage senza cancellarla e rimozione dei limiti UI 50 sessioni/500 eventi. Manifest SHA-256 generato dallo script.
- **Verifiche v6:** CTest 9/9, frontend 9/9, Rust 6/6; build Tauri release riuscita. La compilazione non è una prova smoke UI o di resume: nessuna sessione agente/modello è stata avviata dal bundle v6.
- **Limite architetturale esplicito:** lo store v6 è un JSON snapshot atomico, non ancora SQLite/transcript append-only per record; la UI continua a creare una sessione/run per invio e il backend riceve soltanto il prompt corrente. La cronologia è durevole, ma la continuità che fa ricordare i turni precedenti al modello non è implementata.
- **Non superato:** Gate B di sicurezza/modello, smoke di una sessione backend production, persistenza DB/resume, enforcement Plan e acceptance TaskFlow. L'utente farà i test d'uso della build; eventuali problemi vanno registrati senza trattare il run come PASS automatico.
- **Da verificare con l'utente:** risposta breve italiana a `CI SEI`, assenza di eventi tecnici al centro, nessun verifier per la modalità Conversazione e corretta approval/caducità in Coding.

## Evidenza sul problema `forge.exe`

Il resolver originale in `studio/src-tauri/src/main.rs::find_forge_exe` controllava `FORGE_EXE`, la directory dell'app, la directory genitore e `PATH`. Nel checkout sono presenti:

- `build-agent/Release/forge.exe`
- `build-agent-verify/Release/forge.exe`

Questi percorsi non erano tra quelli cercati quando Studio partiva da `studio/src-tauri/target/release/forge-studio.exe`. Risolto in questa fase via configurazione UI e bundle portabile con `forge.exe` affiancato; il resolver di sviluppo non considera mai `build-agent-verify`.

## Ordine vincolante

Non saltare un gate: in particolare nessun codice generato/test generato va eseguito prima che sicurezza modello e isolamento abbiano prove sufficienti.

### 1. Rendere riproducibile il backend — P0

1. Identificare quale dei due `forge.exe` è la build di produzione approvata; registrare commit/build type, SHA-256, dipendenze native e protocollo JSONL. Non assumere che `build-agent-verify` sia distribuibile.
2. Decidere una sola modalità di installazione supportata: backend accanto a Studio (consigliata per bundle portabile) oppure path configurato dall'utente. Implementare il resolver coerente; mostrare in diagnostica il path effettivamente risolto e l'errore se invalido.
3. Aggiungere test del resolver: env valido/inesistente, exe accanto all'app, package path scelto, PATH, spazi nel path, directory non valida. Verificare che non venga preso un fixture/test binary per errore.
4. Preparare una distribuzione di prova pulita (Studio + backend + runtime richiesto) fuori dal checkout e aprirla senza `PATH` di sviluppo. Non avviare ancora un task che esegue codice.

**Gate A:** Studio trova e avvia il backend previsto in installazione pulita; hash/path visibili e registrati; protocollo start/JSONL/exit verificato con test non generativo o fixture marcato solo test.

### 2. Chiudere sicurezza e licenze — P0, blocca l'esecuzione

1. Recuperare rapporto primario del finding `PAIT-GGUF-100`; stabilire ambito, versione scanner, hash e applicabilità, conservando il rapporto. Risolvere il conflitto tra `models/MODEL.lock` e `LICENSES/NOTICE.md`; non dichiarare Apache per i pesi senza prova.
2. Confrontare hash modello, tokenizer, chat template e token speciali con le fonti/versioni effettivamente usate da llama.cpp; ripetere test del protocollo/tool-call con input avversariali in ambiente controllato.
3. Scegliere/configurare Windows Sandbox, VM usa-e-getta o runner isolato equivalente. Richiedere snapshot workspace, niente credenziali/drive host, rete off per default, limiti di risorse/processi/tempo e log conservati.
4. Eseguire negative containment test: accesso/scrittura fuori workspace, rete, symlink/junction escape, process spawn, timeout, process tree, output eccessivo, crash e cleanup.

**Gate B:** finding e licenza hanno esito documentato accettabile; runner supera tutti i test negativi. In caso contrario: solo test statici/unitari sicuri, niente TaskFlow esecutivo, verdetto massimo PARTIAL.

### 3. Integrare i contratti backend mancanti — P1

1. Versionare schema JSONL e modellare stati/correlazioni per sessione, tool, approval, cancel, exit, error, verifier, recovery e checkpoint.
2. Implementare persistenza locale transazionale per sessioni/eventi/config e migrazioni; non affidare l'unica copia della cronologia a `localStorage`.
3. Implementare Resume Studio dal checkpoint sicuro: identity/hash workspace, stati non riprendibili espliciti e nessun replay di tool non idempotenti o approval pendenti.
4. Implementare Plan come policy del dispatcher (negare e testare write/exec/MCP mutativo), non come pulsante o istruzione al modello.
5. Registrare baseline del workspace e provenance operation/path/hash pre/post per attribuire solo le modifiche della sessione; proteggere save con confronto versione e scrittura atomica.
6. Definire contratto telemetry e renderizzare solo valori misurati; `unknown` per i dati assenti.

**Gate C:** test automatici per migrazioni, eventi correlati, Plan deny, resume senza side effect duplicati e provenance con modifiche preesistenti.

### 4. Completare l'esperienza Studio — P1

- Aggiungere stati accessibili e verificati per offline/backend assente, approval, error, cancel richiesto/confermato, interrupted/resume, no diff e no metrics.
- Completare review/diff reale con baseline, editor sicuro, impostazioni validate e MCP nativo end-to-end. Il terminale PTY è una feature distinta: implementarlo solo se rientra nel criterio di prodotto, senza aggirare dispatcher/approval.
- Allineare la UI ai riferimenti allegati; archiviare screenshot accessibili e verificare layout, keyboard/focus, scaling Windows e dimensioni supportate.
- Disabilitare o etichettare chiaramente le funzioni ancora non supportate; no fake Plan, metrics, review, PTY o resume.

**Gate D:** test UI/accessibilità e smoke Tauri senza backend e con backend production; nessuna funzione dichiarata reale resta solo mock.

### 5. Verifier indipendente e acceptance — P0 prima del TaskFlow

1. Definire prima del run checklist atomica TaskFlow e criteri per continuità e false completion; produrre un verifier esterno che esegua/controlli i criteri e registri prove/hash.
2. Estendere le suite protocollo, approvals/MCP, cancellation, recovery, resume, storage/provenance e bridge production; mantenere i test fixture separati.
3. Fare uno smoke nativo non generativo con il backend production: path/hash, start, eventi, deny/approval, cancel, exit, riavvio Studio. Verificare Gemma/runtime solo localmente, senza provider cloud.

**Gate E:** acceptance harness e verifier sono riproducibili; falsi finali vengono rifiutati; smoke usa il `forge.exe` approvato e nessun mock.

### 6. Run controllati, release e chiusura — P2

1. In workspace disposable isolato, inviare una volta il prompt TaskFlow concordato; niente prompt di continuazione. Conservare log grezzi, tool/file ledger, interventi approval, tempi/token/VRAM solo se realmente misurati.
2. Eseguire i test generati esclusivamente nel sandbox. Verificare separatamente ogni criterio col verifier indipendente.
3. Eseguire poi i run distinti di continuità (stop/restart/resume e no duplicate) e false completion (almeno cinque criteri; il verifier deve bloccare una dichiarazione prematura).
4. Compilare release da directory/installazione pulita, testare avvio senza dipendenze implicite dal checkout/PATH, registrare hash e preparare rollback.
5. Aggiornare acceptance, M0 status, istruzioni installazione e licenze; tenere PARTIAL/FAIL se un gate non passa. Nessun commit automatico.

**Gate finale:** passano i criteri concordati; sandbox e modello approvati; bridge production nativo; resume/approval/provenance corretti; deliverable avviabile autonomamente; report riproducibile e coerente con le evidenze.

## Cosa manca, in breve

1. Il pacchetto portabile v5 è stato costruito e avviato; resta da ripetere lo smoke nativo collegato al backend di produzione in installazione pulita.
2. Gate `PAIT-GGUF-100`, licenza e validazione tokenizer/template.
3. Sandbox reale con test negativi di contenimento; fino al superamento non eseguire codice o test generati.
4. Esperienza unificata senza toggle, intent routing per turno, skill registry, discovery MCP lazy e verifier condizionale: non implementati.
5. Persistenza durevole/resume, enforcement Plan e provenance backend.
6. Approval/MCP nativi end-to-end, review/diff, accessibilità e capability discovery; acceptance produzione ancora assente.
7. Verifier indipendente, TaskFlow, continuità e false-completion acceptance.
8. Release installabile autonoma, diagnostica e report finale coerenti con le evidenze.


## Stato v8 (2026-10-29) � continuita', memoria, istruzioni, skill

- **Continuita' intra-sessione: implementata.** Il composer invia follow-up nella sessione esistente; il backend riceve `history` validata (max 64 messaggi, 256 KiB) e ripete system->cronologia->task. Cronologia capillare conservata nel transcript Tauri; nessuna richiesta di resume multiprocess.
- **Memoria di progetto: implementata.** Archivio `app_data` per workspace con backup, cap 16 KiB, editor con autosave debounced.
- **AGENTS.md: implementato.** Letto dal backend all'avvio del run come dati non attendibili; non concede permessi; indicatore UI.
- **Skill discovery + on-demand: implementata (v8).** `.forge/.agents/.claude/.opencode/skills`; pannello UI con checkbox; il body SKILL.md entra nel blocco istruzioni (cap 32 KiB totale). Le skill restano dati; nessuna permission.
- **Restano fuori:** resume automatico di sessione interrotta al riavvio, compattazione contesto, review/provenance, PTY, metriche, gate `PAIT-GGUF-100`/licenza, sandbox per codice generato, acceptance TaskFlow. Verdetto corrente: **PARTIAL**.
- Test riferiti: CTest 10/10, Rust 8/8, frontend 12/12. Bundle: `artifacts\forge-studio-portable-v8` (manifest SHA256 incluso).


## Stato bundle v9 (2026-10-29)

- Resume esplicito sessione interrotta: implementato nel frontend via replay dei turni completati, bottone Riprendi, status aggiornato; test dedicati. Non ripristina processo/tool call pendenti.
- Compattazione: digest estrattivo deterministico locale dei turni omessi (max 8 KiB, cap 32 KiB con memoria), contatore omissioni visibile; niente sintesi generativa. Test dedicati.
- Verifiche v9: frontend 27/27, Rust 8/8, CTest 10/10, build frontend/Tauri release PASS. Bundle rtifacts\forge-studio-portable-v9, manifest SHA256 incluso.
- Gate complessivo resta **PARTIAL/BLOCCATO**: sandbox negativa non eseguita, verifier TaskFlow non implementato, decisioni su NOTICE e policy redistribuzione pendenti; audit tokenizer/Jinja C++ parziali. Consultare rtifacts\forge-gates-model-license.md, orge-sandbox-design.md, orge-taskflow-acceptance-plan.md.
