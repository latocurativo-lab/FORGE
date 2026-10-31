# FORGE Studio — superpiano redesign + collaudo E2E

**Stato:** piano operativo aggiornato al 29-10-2026; l'implementazione attuale è parziale, la nuova esperienza unificata non è ancora realizzata né accettata.  
**Ambito:** portare FORGE Studio a uso quotidiano: chat, programmazione e task agentici nella stessa conversazione; routing automatico di skill/MCP; stato e approval comprensibili; persistenza, review e verifiche affidabili; acceptance indipendente con Gemma locale.  
**Principio:** FORGE resta proprietaria di runtime, modello, dispatcher, harness, verifier e MCP. OpenCode è riferimento UX e fonte da studiare, non backend sostitutivo.

## Contratto di prodotto: una sola conversazione, nessuna preselezione

La UI non deve chiedere all'utente di scegliere prima tra “chat” e “coding”. Il messaggio corrente, la cronologia della sessione, il workspace e le capacità realmente disponibili determinano il passo successivo. Il router è interno e non è un verdetto irreversibile: il task può passare da spiegazione a lettura, modifica, verifica o uso MCP nella stessa conversazione.

1. **Risposta senza azioni:** saluti, domande e spiegazioni ricevono una risposta diretta; nessuna tool call, approval o esecuzione del verifier solo perché il turno è terminato.
2. **Lavoro sul workspace:** FORGE decide se servono lettura/ricerca; modifica file solo se richiesta o necessaria al task; comunica cosa ha fatto senza dichiarare successo non verificato.
3. **Skill:** una skill è istruzione/procedura specializzata, selezionata dal registry locale in base alla richiesta e caricata solo quando pertinente. Non è un tool e non esegue da sola operazioni.
4. **MCP/tool:** le capacità vengono dalle configurazioni effettive, non da nomi immaginati. L'agente può scegliere il tool pertinente; policy backend/approval controllano la chiamata e i risultati sono dati non attendibili.
5. **Verifica:** si avvia quando il lavoro è stato modificato o quando l'utente chiede di verificare; mai come conseguenza automatica di una risposta conversazionale. L'esito è separato dal testo del modello e mostra comando, exit code e prove.
6. **Chiarimento:** se intento, target o rischio non sono chiari, FORGE chiede una domanda breve prima di agire. Non deve fallire in una modalità sbagliata o eseguire tool “per sicurezza”.
7. **UX:** messaggi user/assistant al centro; attività, skill selezionate, tool e verifiche nel pannello laterale, compatti ed espandibili. Approval necessarie sono l'unica interruzione in primo piano.

La v5 è una build di prova con toggle Conversazione/Coding, non soddisfa questo contratto: in chat non programma e Coding continua a imporre il percorso verifier. Va usata solo per testare quel prototipo, non come definizione dell'esperienza finale.

## 0. Decisioni e stato iniziale verificato

- Il checkout contiene modifiche e file non tracciati preesistenti: `.gitignore`, `agent/`, `studio/`, `mcp/`, `runtime/`, `artifacts/` e altri. Prima di ogni modifica codice si acquisisce una baseline con `git status --short`, hash e inventario; non si fa reset, checkout o pulizia indiscriminata.
- Studio è oggi una singola schermata React/Tauri: workspace, Monaco, sessioni/eventi e configurazione agente sono parziali; cronologia/config sono in `localStorage`, massimo 50 sessioni, non esiste resume fedele. La UI non è ancora la UX richiesta.
- Il bridge `start_agent` avvia il `forge.exe` trovato a runtime e trasporta JSONL; ha start/cancel/approval, ma non un comando di resume. CLI FORGE supporta `forge resume` con checkpoint. Verificare se e come lo stato sia esposto al frontend prima di promettere ripresa.
- Plan/Build, shell interattiva, revisioni attribuite alla sessione, tab persistenti, menzioni, ricerca simboli e metriche runtime non risultano implementati come flussi completi.
- Stato aggiornato: CTest agente 9/9, test frontend 9/9, Rust 4/4 e build/package Tauri v5 passati; `forge-studio.exe` dal bundle v5 è stato avviato e risponde. Questi test non sono acceptance della conversazione con Gemma. Il precedente smoke UI nativo usava un fixture bridge, non prova la UI collegata al backend di produzione. L'E2E Gemma precedente controllava marker strutturali e non eseguiva `node:test`.
- La v5 introduce temporaneamente modalità Conversazione/Coding, nessun tool/verifier in chat e timeout approval 30s. È un workaround esplicito per il verifier sempre avviato, non il requisito finale; nessun task Gemma reale è stato inviato.
- Nell'implementazione corrente i server MCP configurati possono esporre tool al backend, ma non esiste ancora un registry/runtime di skill che carichi automaticamente istruzioni pertinenti. La configurazione MCP è ancora JSON manuale e manca discovery/configurazione guidata end-to-end.
- `PAIT-GGUF-100` è ancora aperto: l'hash del file combacia con l'OID pubblicato, ma questo non certifica template/tokenizer, pesi o sicurezza. I tool Windows attuali non sono una sandbox. Nessun codice generato va eseguito finché non è stato predisposto e collaudato un confine di isolamento adeguato.
- Benchmark contesto: 4K–32K con tre repliche; 64K/128K con un trial esplorativo. Il probe 128K ha prodotto 16 token con `finish_reason=length`: non dimostra qualità o ripetibilità a contesti lunghi.
- Binario attualmente presente: `studio/src-tauri/target/release/forge-studio.exe`; non è ancora il deliverable del redesign.

### Riferimenti di prodotto consultati (29-10-2026)

Questi riferimenti descrivono pattern da confrontare, non autorizzano copia di codice o asset e non implicano che FORGE debba replicare funzioni cloud/proprietarie:

- OpenCode V2: [TUI](https://opencode.ai/v2/docs/cli/tui), [istruzioni](https://opencode.ai/v2/docs/instructions), [skill](https://opencode.ai/v2/docs/skills), [compaction](https://opencode.ai/v2/docs/compaction), [agents](https://opencode.ai/v2/docs/agents), [API](https://opencode.ai/v2/docs/api), [SDK](https://opencode.ai/v2/docs/build/sdk).
- Claude Code: [sessioni e resume](https://code.claude.com/docs/en/sessions), [memoria e instructions](https://code.claude.com/docs/en/memory), [skill](https://code.claude.com/docs/en/skills), [Desktop Code](https://code.claude.com/docs/en/desktop).
- Pattern convergenti: transcript persistente per sessione; resume/branch distinti; istruzioni di progetto scoperte con precedenza visibile; skills descritte e caricate on-demand; context compaction senza cancellare il transcript; indicatori e recovery chiari.
- Decisione FORGE: transcript, memoria, istruzioni e skill sono quattro oggetti separati. L'app deve dichiarare quali istruzioni/skill ha caricato e perché. Il testo di istruzioni o memoria non costituisce policy di autorizzazione; la sicurezza resta imposta da dispatcher/bridge.

## 1. Riferimento OpenCode e licenze

### Fonti già consultate

- Repository ufficiale: <https://github.com/anomalyco/opencode> — per il riferimento UX V2 l'audit indipendente ha identificato la release `v2.2.0` (27-10-2026), tag commit `1097e2668b1e08ad63bdf0b1c4860b9bc01e420c`; `@opencode/desktop` e `@opencode/app` dichiarano `2.2.0` e MIT a quel commit. La release V1 `v1.19.5` è un track distinto e non va confusa con la reference V2. La pagina desktop beta può pubblicare build branch-specific; `v2.2.0` è la reference immutabile identificata, non un'affermazione che ogni beta installata sia quella versione. Root license MIT; audit dipendenze/asset resta obbligatorio.
- Docs richieste: <https://opencode.ai/docs/> e <https://opencode.ai/docs/agents/>. Nel contenuto corrente compare il banner “New OpenCode v2”; `/docs/` contiene materiale V1 e rimanda esplicitamente a V2. La pagina agenti descrive Build primario con accesso agli strumenti, Plan primario con permessi ridotti, agenti secondari, permessi e menzioni. I default di Plan vanno letti dalla documentazione/config effettiva, non copiati come assunzione.
- Riferimenti visuali principali: tre schermate allegate dall'utente (home, sessione e revisione); prima di fissare il layout si archiviano nel design brief con note su gerarchia, densità, larghezze, affordance e stati visibili.
- Stato screenshot in questa sessione: non sono stati trovati come file nel checkout; fare l'ispezione visiva effettiva a partire dagli allegati accessibili prima del sign-off UI. Non dichiarare confronto pixel-by-pixel finché non si dispone delle immagini.

### Gate obbligatorio prima di riuso o citazione di una “versione”

1. Registrare data UTC, URL, tag/release, commit SHA e package/versione del desktop frontend effettivamente selezionato; distinguere app desktop beta, frontend web e ramo di sviluppo.
2. Verificare `LICENSE`, licenze dei package/componenti, avvisi per asset/font/icone e dipendenze transitive. La licenza MIT del repository non prova automaticamente che ogni asset/dipendenza abbia gli stessi termini.
3. Preferire reimplementazione indipendente di pattern UX. Se si copia codice, limitare la quantità, conservarne gli header copyright/licenza, attribuire in `LICENSES/NOTICE.md` e fare review delle condizioni di redistribuzione.
4. Congelare in `artifacts/reference/opencode-reference.md` URL, commit/tag, schermate e decisioni. Se la versione esatta non è dimostrabile, dichiararlo e non inventare un numero.

## 2. Obiettivi e non-obiettivi

### Obiettivi di prodotto

- App Windows dark-first, compatta e professionale, centrata su progetti, sessioni e conversazione; identità FORGE propria e non clone di VS Code.
- Timeline degli eventi reali del dispatcher, tool call inline espandibili, approvazioni esplicite, revisione/diff con provenienza, editor e terminale come pannelli facoltativi.
- Plan/Build applicati dal backend/harness con policy verificabili, non solo da prompt o colore UI.
- Sessioni persistenti e recuperabili senza replay di azioni non sicure; configurazione persistente e validata.
- Telemetria solamente da fonti misurate; UI che esplicita “non disponibile/non misurabile” invece di inventare valori.
- E2E riproducibile su workspace temporaneo con Gemma 4 12B Q4_K_M, runtime e harness FORGE, senza prompt di continuazione manuali.
- Un'unica conversazione agentica senza scelta preventiva chat/coding; instradamento osservabile e reversibile a livello di turno, con tool/skill/MCP utilizzati solo se pertinenti e autorizzati.
- Registry locale versionato per skill, MCP, policy e profili di verifica; la UI spiega le capacità effettivamente disponibili e non inventa integrazioni assenti.
- Verifier condizionale: nessun verifier dopo una risposta o una semplice lettura; test/verifica dopo modifiche o richiesta esplicita, con criterio e comando stabiliti in modo controllabile.

### Non-obiettivi / vincoli

- Non migrare a OpenCode, non sostituire Gemma o il dispatcher, non usare provider cloud per il task di accettazione.
- Nessuna simulazione di terminale, tool call, attività, diff, metriche, checkpoint o sessioni.
- Nessun “PASS” derivato dal solo rendering, dall'esistenza dei file, da marker superficiali o dalla risposta finale del modello.
- Nessuna esecuzione di output generato su host non confinato. Se il sandbox gate non passa, il task di generazione può essere osservato senza run di codice, ma il risultato globale non può essere PASS.

### Definizione misurabile di “100% usabile”

“100%” non significa assenza di bug o garanzia di sicurezza assoluta. Per questa consegna significa che tutte le capacità incluse sotto sono implementate realmente, raggiungibili senza modalità artificiali, persistono dove promesso e hanno test automatici + smoke Tauri nativo sul `forge.exe` di produzione. Un'etichetta, un fixture o un controllo manuale non sostituiscono l'integrazione.

- **Percorso quotidiano:** una sola conversazione supporta chat, domande sul workspace, coding, skill locali, MCP configurati e verifica condizionale. Nessun toggle chat/coding e nessun verifier su saluti/risposte non mutative.
- **Affidabilità:** sessione/eventi/config salvati in store durevole; interruzione, timeout, cancel, chiusura e riavvio hanno esito esplicito; resume non ripete effetti incerti.
- **Continuità/memoria:** invii successivi restano nella stessa sessione e il modello riceve contesto precedente ricostruito; transcript completo sopravvive a riavvio; memoria utente/progetto è distinta, ispezionabile e cancellabile; context overflow compatta senza perdita del transcript.
- **Istruzioni/capacità:** instruction files effettivi mostrati con provenienza; AGENTS.md e skill standard scoperte con precedenza e limiti deterministici; skills caricate solo se pertinenti e mai trattate come autorizzazione.
- **Controllo:** capacità invocate solo se disponibili e pertinenti; permessi verificati dal backend; approval correttamente correlata, scaduta o negata non può trasformarsi in approvazione.
- **Comprensibilità:** chat leggibile, tool/progress/diff nel pannello attività/review, verifier con prova, errori con recupero chiaro; nessuna metrica o capacità inventata.
- **Consegna:** bundle avviabile da installazione pulita, README essenziale, hash/versioni, log e report acceptance coerenti. Nessun cloud provider per Gemma.

La disponibilità dell'app per chat, lettura e modifica non autorizza a eseguire codice/test generati: quello resta soggetto al Gate G1. Il report può dichiarare “uso quotidiano per le capacità verificate” solo quando tutte le righe di prodotto passano; il verdetto M0/TaskFlow rimane PARTIAL/BLOCKED finché audit, sandbox e acceptance indipendente non passano.

## 3. Fasi, dipendenze e gate

Le fasi possono avere lavoro UI indipendente in parallelo solo dopo la baseline. I gate di sicurezza e produzione bloccano ogni prova che esegua output generato.

### Fase 0 — Baseline, inventario e protocollo di prova

**Attività**

1. Congelare stato iniziale senza alterare modifiche preesistenti: `git status --short --branch`, elenco processi/runtime, commit/dirty state se disponibile, hash SHA-256 di binari/modello/config, versione toolchain e dipendenze.
2. Mappare UI, bridge Tauri, eventi JSONL, lifecycle dei processi, verifier, permessi, checkpoint e MCP. Per ogni feature richiesta segnare: disponibile, parziale, assente, sorgente dati e test attuale.
3. Estrarre e annotare gli screenshot di riferimento senza copiare asset OpenCode; scrivere un design brief con layout home/sessione/review e breakpoints desktop.
4. Acquisire fixture/riproduzioni dei difetti noti prima di correggerli. Tenere separato il registro “preesistente”, “scoperto in questa attività” e “corretto”.

**Gate G0:** inventario ripetibile; baseline e manifest salvati; nessun test usa un repository utente.

### Fase 1 — Gate sicurezza modello, tool e sandbox (P0)

**Audit `PAIT-GGUF-100` e provenienza**

1. Raccogliere output originale dello scanner e identificare finding/versione scanner, file, hash e significato specifico dell'ID; non inferire “sicuro” da scanner discordanti o dal campo `safe`.
2. Verificare hash locale, origine/licenza del modello, tokenizer e template contro i file ufficiali e le note di llama.cpp; analizzare gli override dei token di controllo registrati nel log.
3. Testare in ambiente controllato casi di escaping dei marcatori, tool-call injection e delimitatori malevoli; conservare prompt, output grezzi, versione runtime e analisi. Non fare test offensivi su sistemi esterni.
4. Redigere threat model per prompt injection, output modello, file workspace, processi figli, MCP, rete, segreti, symlink/junction, race filesystem e escape sandbox.

**Sandbox per programmi e test generati**

1. Valutare disponibilità e configurazione reale di Windows Sandbox, Hyper-V/VM isolata o runner disposable equivalente; documentare ciò che è abilitato sul PC. Container senza isolamento kernel adeguato, Job Object, allowlist executable, temp folder e utente non privilegiato da soli non si qualificano automaticamente come sandbox.
2. Definire policy minima: workspace snapshot usa-e-getta; niente credenziali/drive host; rete disabilitata per default; limiti CPU/RAM/tempo/output/processi; filesystem allowlist; processo terminato e snapshot scartato; log e exit code preservati; test del tentativo di accesso fuori workspace/rete.
3. Implementare un runner confinato separato dal normale `EXEC`, con richiesta/approvazione distinta, path canonici, rifiuto symlink escape, timeout e cleanup affidabile. Il verifier esterno deve poter verificare il risultato senza eseguire codice non isolato.
4. Eseguire suite negativa (scrittura fuori, accesso rete, spawn, timeout, process tree, output enorme, crash) prima di usare il runner sul task TaskFlow.

**Gate G1:** audit finding documentato; nessuna criticità irrisolta che renda insicuro l'uso agentico previsto; runner sandbox dimostrato con test di contenimento. Se il gate non passa, vietata esecuzione di codice/test generati e l'E2E complessivo sarà al massimo PARTIAL/FAIL secondo i criteri finali.

### Fase 2 — Contratti backend e semantica delle funzioni

1. Definire capability matrix tra UI e backend, mantenendo compatibilità JSONL dove possibile. Ogni campo/evento nuovo deve avere schema, versione, correlazione sessione/call/request e test backward-compatibility.
2. Rendere eventi sufficienti alla timeline: user/task, agent message/final, tool start/result/error, durata, stdout/stderr/exit code, approval request/decision, verifier, retry/recovery, cancellation, checkpoint, runtime usage, session state. Emissione solo quando l'evento è reale.
3. Modellare stati monotoni e terminali: `queued → running → waiting_approval / recovering → completed | failed | cancelled | interrupted`; definire timeout, exit code e differenza tra cancel richiesto, confermato e processo terminato.
4. Aggiungere session ID durevole e store locale transazionale (Tauri/SQLite o formato journal atomico da decidere dopo l'audit). Registrare messaggi/eventi/checkpoint/config/workspace/hash modifiche, migrazioni schema, retention e protezione dati.
5. Definire resume esplicito: salvare checkpoint in percorso noto, validare workspace identity e hash/stato, riprendere solo da stato safe, mai replay di tool non-idempotenti o approval pendenti. UI distingue “riprendi dal checkpoint”, “nuovo tentativo” e “non riprendibile”.
6. Implementare Plan come capability/policy enforcement nel dispatcher: lettura/lista/search consentite; mutazioni file, shell e MCP mutativo negate dal backend (o richiesta approvazione secondo policy), non semplicemente escluse dalla UI. Transizioni Build/Plan registrate nella sessione.
7. Definire Git baseline al momento della sessione e ledger di modifiche FORGE: path, hash pre/post, operation ID, timestamp, tool/call ID. Non attribuire come proprie modifiche preesistenti o concorrenti. Proteggere sovrascritture con compare-before-write e salvataggio atomico.
8. Verificare quali metriche siano esposte dal server: modello/quantizzazione/contesto, prompt/completion token, prefill/decode throughput, tempi, VRAM per processo o globale, KV cache. Esprimere fonte, unità, frequenza, precisione e `unknown`; non inferire per differenza.
9. Definire protocollo di turn/sessione che distingue messaggio finale, richiesta di tool, mutazione riuscita, richiesta di verifica e risultato verifier. `assistant_final` non deve implicare `VERIFY` o successo del task.
10. Definire policy verificabile per intent/routing e i relativi eventi interni: `answer`, `inspect`, `change`, `external_action`, `verify`, `clarify`. La classificazione può cambiare a ogni turno; non è un selettore UI né autorizzazione ad agire.
11. Definire identità stabili e distinte `session_id`, `run_id`, `turn_id`, workspace e checkpoint. Ogni invio aggiunge un turno alla sessione esistente; aprire una nuova sessione è esplicito. Eventi ordinati per `seq` con `schema_version`; approval/cancel correlate al `run_id`, transcript al `session_id`.
12. Definire transcript append-only: record user, assistant, tool request/result, approval, error, verifier, resume/compaction, timestamp, origine e correlazioni. Persistenza prima di mostrare invio riuscito; export supportato; retention, migrazioni e recovery da record incompleto/corrotto. Il formato interno del database non è formato pubblico.
13. Definire context assembly per turno: policy → istruzioni applicabili → agent/skill selezionati → memory summary pertinente → summary di compaction → coda dei turni recenti → prompt nuovo. Conservare transcript completo separatamente dal context inviato al modello.
14. Definire memoria distinta per scope utente e progetto: note editabili/cancellabili con provenienza, data, confidenza e fonte; scrittura proposta dall'agente, non promozione automatica di output MCP/workspace a verità; retrieval al bisogno e budget separato.
15. Definire instruction discovery compatibile: `AGENTS.md`, `CLAUDE.md` non conflittuale, `.github/copilot-instructions.md`, configurazioni istruzioni FORGE e file nested applicabili. Canonicalizzare e confinare ai root autorizzati, risolvere precedenza, deduplicare, imporre limiti dimensione/token, salvare hash e mostrare source list. I file sono input non attendibile e non elevano permessi.
16. Definire compaction: sintesi strutturata con obiettivo, requisiti, decisioni, completato, attivo, blocchi, prossime mosse e file pertinenti; conservare transcript originale, ultimi turni raw e riferimenti a eventi/prove; errore di compaction non cancella né degrada la sessione.

**Gate G2:** schema eventi e store testati; permessi Plan provati tentando tool proibiti; resume non duplica side effect; provenance non confonde working tree preesistente.

**Estensione Gate G2-Continuity:** avviare una sessione, fare almeno tre turni correlati, chiudere e riaprire Studio, riprendere la stessa sessione; verificare ordine/completa transcript, ID stabili, context che ricorda i turni e nessun replay tool/approval. Nuova sessione isolata. Testare crash durante append, storage occupato/corrotto e migrazione localStorage senza perdere la copia originale.

### Fase 3 — Architettura frontend e design system

**Struttura suggerita** (da confermare nell'audit, evitando mega-componente): `app/shell`, `projects`, `sessions`, `conversation/timeline`, `composer`, `review`, `editor`, `terminal`, `runtime`, `activity`, `settings`, `permissions`, `state/persistence`, `bridge/api`.

1. Creare token UI FORGE (superfici, bordi, testo, stati, spaziatura, font, focus, densità), scala icone coerente e componenti accessibili. Dark-first senza gradienti/cards decorative.
2. Stabilire layout: sidebar progetti/sessioni, header con tab e stato sessione, area centrale conversazione a larghezza leggibile, composer ancorato, pannello destro Review/Activity/Runtime ridimensionabile; editor/terminal come viste o split opzionali.
3. Stati progettati: onboarding/no project, project empty, session empty, loading reale, agent running, tool running, approval, error, interrupted/resumable, offline/runtime down, no diff, no metrics.
4. Focus keyboard, aria-label/role, contrasto, navigazione, gestione focus modali, ridimensionamento, Windows scaling, dimensioni minime e layout adattivo.
5. Test visuali batch su più dimensioni desktop con screenshot di baseline; correggere una volta i difetti emersi e ripetere un solo pass di conferma.

**Gate G3:** design review contro i tre screenshot, nessuno stato essenziale mancante, test accessibilità/keyboard e layout passano.

### Fase 4 — Home, progetti, sessioni e navigazione

- Registro progetti: aggiungi/apri cartella, rimuovi solo dall'elenco, ricerca, menu contestuale, stato sessioni, persistenza e validazione percorso.
- Home sessioni raggruppate Oggi/Ieri/7 giorni/Più vecchie; titolo automatico modificabile; crea/apri/rinomina/duplica solo se backend supporta copia sicura/archivia/elimina con conferma/ricerca/riprendi.
- Tab sessioni multiple persistenti, tab attiva, chiusura tab senza eliminare sessione, navigazione back/forward, nuova sessione, scorciatoie e focus.
- Nessun “duplicato” finto: se la semantica non esiste implementare copia di messaggi come nuova sessione solo se chiaramente etichettata, altrimenti disabilitare con motivazione.

**Gate G4:** riavvio app ripristina progetti, tab e sessioni in modo coerente; rimozione progetto non cancella file; operazioni distruttive richiedono conferma.

### Fase 5 — Conversazione, timeline, composer, Plan/Build

- Rimuovere il toggle Conversazione/Coding. Un solo composer e una sola sessione; il backend sceglie il percorso per ogni turno e conserva il contesto conversazionale tra risposte, letture e modifiche.
- Un invio dal composer nella sessione attiva crea un nuovo `turn_id` nella sessione esistente, non una sessione nuova. Nuova sessione, riprendi, branch e fork hanno azioni e identità distinte. Transcript centrale ricostruisce user/assistant; vista normale comprime tool in card espandibili e vista Verbose mostra gli eventi diagnostici.
- Alla ripresa reinviare al modello transcript/summary secondo budget, non il JSONL grezzo né il solo prompt ultimo. Se manca una parte dello stato, mostrare `ripresa parziale/non disponibile` e offrire scelta; mai fingere continuità.
- Activity mostra `context sources`: file istruzioni, skill ID/version/hash, memory note ID e compaction summary; `/context` mostra fonti incluse/escluse e uso token solo se misurato.
- Scrivere un intent router con segnali espliciti (richiesta dell'utente, contesto, workspace e capacità disponibili), output schema-validato e comportamento `clarify` in caso di incertezza. Non eseguire classificatore solo per saluti o risposte semplici se il modello può rispondere direttamente; non passare reasoning privato in UI.
- Tool schema disponibile solo nel percorso agentico; una risposta normale non presenta eventi di start/generation/verifier nella chat. Nel percorso coding il modello può rispondere senza tool se non servono; nessuna scelta manuale obbligatoria.
- Definire trigger verifier: 1) almeno una mutazione file/processo attribuita a questa sessione, oppure 2) richiesta esplicita di test/verifica. Lettura/esplorazione e risposte non lo attivano. Se non c'è un verifier affidabile configurato o sandboxata, segnalare “non verificato” e chiedere/aspettare una decisione, non eseguire `where.exe` o un comando incoerente.
- Timeline principale di soli messaggi user/assistant. Activity laterale mostra eventi reali raggruppati per task/tool; approval pendenti emergono immediatamente, attività non interattive non spostano la chat. Rendering finale in lingua richiesta, non un dump delle fasi interne.
- Timeline cronologica per activity summary da eventi osservati, tool, result, error, approval, verifier e final. Nessun chain-of-thought inventato; se reasoning non è separato, mostrare “attività osservata” o riepilogo dichiarato come tale.
- Tool row compatta con icona/nome/descrizione/stato/durata; expand input/output redatti, stdout/stderr/exit code, MCP server/tool/args/result e diff vero. Virtualizzazione se eventi numerosi; filtri e copia.
- Composer multilinea auto-resize, Enter invia e Shift+Enter newline, history, paste code, drag/drop solo file ammessi, `@` autocomplete da fonti realmente indicizzate/disponibili, slash command con risultati reali.
- Comandi: `/init` analizza e propone/crea file solo secondo permessi; `/plan` e `/build` cambiano policy; `/clear` crea sessione nuova; `/compact` solo se esiste una strategia di compaction verificabile; `/status` legge runtime/session state; `/mcp` elenca server effettivi. Comandi non supportati non fingono successo.
- Modello mostrato come `Gemma 4 12B Q4_K_M` solo quando rilevato; parametri ragionamento solo se runtime li supporta, con valore/config effettivi.
- Interrupt: bottone stop, feedback di cancel-requested e conferma di process-exit; nessuna falsa indicazione di arresto.

**Gate G5:** test integrazione per messaggio→Gemma locale→tool→risultato→verifier e Plan read-only; cancellazione concorrente e risposte finali premature non superano verifier.

**Gate G5-U — conversazione unificata:** senza toggle e senza cambiare sessione, una sequenza di prova copre: saluto (“CI SEI”) senza tool/verifier; domanda su un file con sola lettura se necessaria; richiesta di modifica con approval appropriata e diff; “esegui i test” che attiva il profilo verifier se disponibile; domanda successiva che torna a chat. Ogni passaggio produce messaggi naturali e concisi, eventi tecnici solo nel pannello Activity e nessuna azione non richiesta.

### Fase 6 — Review, editor, terminale, attività e runtime

**Review**: changed/added/deleted, status Git, contatori calcolati dal diff, ricerca, diff unificato/affiancato, accept/reject solo dove esiste operazione reversibile e provenienza verificata, open in editor. Confronto con baseline per evitare contaminazione da modifiche utente.

**Editor**: Monaco già presente; tab file, apertura da tree/mention/review, ricerca file/testo, save atomico compare-before-write, dirty state, diff e diagnostica solo se language service disponibile. Conversazione resta prioritaria.

**Terminale**: PTY reale (non pipe non interattiva), PowerShell configurabile, cwd workspace, input/output, scrollback, copia, interrupt, exit code. Separare processi manuali e agentici; l'agente continua a passare dal dispatcher/approval/sandbox, non dal terminale utente.

**Activity panel**: piano registrato, step/indici effettivi se emessi, ultima tool call, verifier, recovery reason e checkpoint. No piano inventato se backend non lo fornisce; stato non disponibile esplicito.

**Runtime panel**: modello/quantizzazione/backend/device/VRAM/contesto/tok/s/KV solo da sorgente verificata; polling e permessi minimi; mostra globale vs per-process, timestamp e campionamento. Picco non misurato = sconosciuto.

**Gate G6:** test reali su file diff, PTY e process lifetime, metrics source contract; nessun dato fixture nella build prod.

### Fase 7 — Approvazioni, MCP e impostazioni

- Approval in timeline: tool, operazione, path/risorsa, args redatti, capability/mutazione, rischio basato su policy dichiarata, motivazione fornita dal backend se presente. One-shot/deny e allow-session solo se policy lo consente; session grant non diventa permanente.
- MCP: discovery effettiva, approvazione server launch separata da call, approval per ogni call mutativa, rifiuto senza invocazione, timeout/error/cleanup, secrets redatti, output trattato come dato non attendibile. Validare max server e path/args; lifecycle processi registrato.
- Skill registry (funzione distinta dal MCP): specificare formato manifesto (id, descrizione, trigger/metadata, versione, percorso, hash, trust source), roots consentite e precedenza; lista/search/preview skill disponibile; caricamento automatico solo delle istruzioni pertinenti al turno; limiti di token; cache invalidata su hash/versione; nessuna istruzione skill può elevare permessi o sovrascrivere policy di sistema.
- Instruction/memory center: mostra file istruzioni caricati/skipped con ragione/hash; skill disponibili vs selezionate; memoria utente/progetto con preview/edit/delete; summary di compaction collegato ai turni originali. Ogni scrittura o rimozione memory ha provenance e approval esplicita.
- Intent-to-capability map: per ogni richiesta instradata, registrare in activity quale skill (se presente) è stata caricata, quale MCP server/tool è stato scelto e la motivazione breve rivolta all'utente. Non mostrare catena di pensiero; distinguere skill non trovata, MCP non configurato, tool negato, tool fallito e timeout.
- UX capability discovery: strumenti/skill installati, abilitati, disponibili e non disponibili chiaramente distinti; installazione/aggiornamento di skill non esegue contenuto automaticamente. Autorizzazioni MCP least-privilege, segreti redatti, tool result trattati come dati non attendibili.
- Lazy MCP: non avviare server MCP a ogni nuova conversazione né per un saluto. Prima classificare la necessità; esporre/avviare solo server candidati pertinenti, richiedere launch approval quando la policy lo impone, poi elencare tool e invocare quello adatto. Le capability mancanti producono una spiegazione, non tentativi a vuoto.
- Policy approval graduata: letture limitate al workspace possono essere automatiche solo dopo decisione esplicita e test di containment; scritture, processi, rete e MCP mutativi richiedono grant specifico per azione, scadenza e correlazione. “Approva” non modifica allowlist persistenti né approva azioni future.
- Settings sezioni: aspetto, modello, inferenza, contesto, agenti, strumenti, MCP, permessi, terminale, Git, workspace, diagnostica. Persistenza, schema versioning, validazione, error messages e reset scoped.
- Shortcut configurabili: new session, search, open project, Build/Plan, review, terminal, editor, file search, interrupt, settings; documentate nei menu/tooltip e testate per conflitti Monaco/Windows.

**Gate G7:** test MCP locale di discovery, launch approval, call approval, success, errore, deny e prova che senza approval non accade alcuna chiamata; segreti non compaiono nella timeline/log.

**Estensione Gate G7-Skills:** test registry (manifesto valido/invalido, precedenza, hash, skill duplicata/malformata), selezione pertinente/non pertinente, omissione per semplice saluto, caricamento limitato, invalidazione cache, skill malevola che tenta privilege escalation e mancata modifica della policy. La stessa suite dimostra routing chat→file→MCP nello stesso thread senza toggle.

**Dataset di routing:** versionare esempi italiani/inglesi con saluti, domande generiche, spiegazione di codice incollato, lettura file, richiesta di modifica, refactor multi-file, richiesta di test, uso esplicito di skill, uso MCP read-only/mutativo, ambiguità, prompt injection nel workspace/risposta MCP, capability non configurata e follow-up che cambia intento. Ogni caso definisce tool attesi/vietati, skill attesa, verifier atteso/non atteso e risposta visibile. Zero side effect nei casi che non li chiedono; accuracy/falsi positivi misurati, non proclamati dal prompt.

**Gate Continuity/Memory/Instructions:** quattro turni nella stessa sessione con riferimenti incrociati; resume dopo riavvio; sessione nuova non eredita transcript privato; memory user/project isolate e cancellabili; AGENTS root + nested path-scoped applicate/deduplicate; conflitti, limiti ed encoding error visibili; skill pertinente caricata una volta e body irrilevante assente; compaction conserva transcript, riferimenti a diff/tool/prove e risponde correttamente a un follow-up su informazione precedente.

### Fase 8 — Verifier, harness e test automatici

1. Costruire verifier indipendente dal modello: criteri atomici, esito per requisito, motivazione, prove, hash e command identity. La risposta “fatto” di Gemma non è condizione sufficiente.
2. Per TaskFlow, checklist derivata dal brief: CRUD, toggle completamento, priorità, filtri, persistenza locale, TypeScript, struttura progetto, UI responsive, test automatici, documentazione; ulteriori criteri osservabili (stati vuoti/errori/accessibilità minima/build) definiti prima del run.
3. Eseguire build e test dentro sandbox approvata e riproducibile; verificare che test provengano dal workspace prodotto, exit code 0 e output archiviato. Non correggere silenziosamente il progetto durante la valutazione.
4. Aggiungere test unitari/integration: protocol JSONL, tool parser, verifier false completion, recovery, cancel, resume/checkpoint unsafe, approvals correlated IDs, storage migration, provenance, MCP fake local, production bridge.
5. Test frontend: reducer/event timeline, session tabs, search/grouping, settings validation, dirty/save, accessible navigation e visual states; evitare mock per dichiarare funzionalità integrate.

**Gate G8:** suite automatica verde; falsi finali bloccati/verificati; report verifier riproducibile e immutabile.

### Fase 9 — Build, integrazione nativa e prova indipendente

**Preflight**

- Workspace univoco `%TEMP%\forge-e2e-<guid>\` con cartelle workspace, run, evidenze e verifier; rifiutare directory non vuote/riuso checkpoint unsafe.
- Registrare hash/versione di `forge-studio.exe`, `forge.exe`, runtime llama.cpp, GGUF, UI bundle, config, toolchain e verifier; output runtime limitato a loopback e nessun endpoint cloud configurato. Verificare rete/proxy e log richieste.
- Verificare modello effettivamente caricato in locale, quantizzazione, SHA, runtime process e provider; non assumere basandosi su etichetta UI.
- Testare app Tauri nativa collegata al `forge.exe` di produzione: progetto, nuova sessione, chat Gemma, tool, diff, approval, cancel/resume, persistenza, editor save, terminale e pannelli. Fixture ammessi solo in test mirati separati, mai come prova production.

**Run principale TaskFlow**

- Inserire una sola volta e letteralmente l'incarico fornito dall'utente; nessun suggerimento/prompt di continuazione manuale. Registrare tutti gli eventi dall'avvio alla terminazione.
- Dare a FORGE i permessi e il verifier predefiniti deliberati prima del run; l'approvazione umana di sicurezza necessaria va contata e documentata, non confusa con prompt di continuazione.
- Se sandbox disponibile: approvare solo le operazioni previste e consentire build/test isolati. Se non disponibile: non eseguire test/build generati; archiviare tentativo limitato e segnare gate bloccato, senza PASS.
- Verifier esegue criteri atomici e ispeziona artefatti; test reali e UI si verificano nel sandbox. Registrare tool calls, file read/write, model turns, retries, protocol errors, malformed calls, loops, wall time, token, VRAM con limiti di misura.
- Distinguere autonomia del modello (tool call/turni scelti dal modello), recovery del harness, intervento umano, risposta di approval e resume/restart orchestrato.

**Continuità**

- Secondo task multifile, workspace nuovo e criteri propri. Interrompere deliberatamente dopo numero prefissato di azioni osservate; preservare log/checkpoint/snapshot; riavviare app/backend; verificare riconoscimento degli output, nessuna duplicazione, resume corretto e criterio finale.
- Se il protocollo non consente resume sicuro dalla UI, il test fallisce quella capacità; non simulare con nuova sessione o replay integrale.

**False conclusioni**

- Task con almeno cinque requisiti indipendenti e verifier costruito prima del run. Usare una modalità controllata che rende verificabili le mancanze senza sabotare file o alterare runtime; registrare se il modello dichiara completion prematura.
- Deve prevalere il verifier: ripresa autonoma se supportata, oppure stato blocked/partial con motivazione. Il solo messaggio finale non dà successo.

**Gate G9:** tutti i flussi dichiarati “reali” superano test nativo su backend di produzione. Feature non finite vengono disabilitate o esplicitamente marcate non disponibili; nessun fixture o mock nei risultati di produzione.

### Fase 10 — Consegna e supportabilità

- Build release pulita e riproducibile; eseguibile + eventuali runtime/assets richiesti; versioni/hash e istruzioni avvio; nessuna dipendenza implicita da un path di sviluppo.
- `artifacts/forge-e2e-acceptance.md`: versione esatta componenti, hardware, modello/hash, configurazione, test per test PASS/FAIL/BLOCKED, comandi riproducibili, log/evidenze, metriche con sorgente, interventi umani, recuperi, problemi, requisiti non soddisfatti, stato sicurezza, verdetto.
- Conservare run raw, screenshot UI, JSONL, log server/stdout/stderr, verifier per requisito, exit code, hash output e ledger approvals senza segreti.
- Aggiornare `artifacts/m0-status.md`, `studio/README.md`, protocollo e notice/licenze senza cancellare le evidenze preesistenti; non fare commit senza richiesta.

**Gate G10:** binario apribile su macchina prevista; test e report concordano; limitations e rollback note consegnate.

## 4. Matrice dei requisiti → verifica

| Area | Evidenza minima richiesta | Stato iniziale |
|---|---|---|
| OpenCode/reference/version/licenze | commit/tag/versione verificata, screenshot note, attribution se codice riusato | Riferimento UX/documentazione consultati; versione desktop immutabile da fissare |
| Home/progetti/sessioni/tab | test native su add/open/remove/search/group/rename/archive/delete/tab/reopen | Parziale; storico localStorage non fedele |
| Conversazione unica + intent routing | test sequence chat→workspace→coding→verify→chat senza toggle, routing non mutativo su saluto | NON IMPLEMENTATO; bundle v5 ha selettore Conversazione/Coding come workaround |
| Skill registry/caricamento | fixture skill pertinenti/non pertinenti, manifest/hash, token budget, injection non può elevare policy | ASSENTE; nessun registry o loader automatico FORGE |
| MCP lazy discovery | nessun server avviato su saluto; discovery/launch solo dopo intent pertinente, call correlate e approval | NON IMPLEMENTATO; MCP startup attualmente legato all'avvio Coding e JSON configurato a mano |
| Conversazione/tool inline | messaggi centrali puliti; eventi reali correlati/espandibili in Activity, stdout/stderr/exit code e failure state | Parziale: v5 filtra messaggi finali, activity grezza laterale; tool inline/diff non completo |
| Composer/mentions/commands | tastiera, autocomplete da fonti vere, ogni slash command testato | Assente/parziale |
| Build/Plan | test negativo backend per write/exec in Plan | Modalità non presente nell'interfaccia/contratto verificato |
| Verifier condizionale | niente VERIFY su chat/read-only; esecuzione dopo mutazione o richiesta esplicita; profilo/id/comando/error/exit registrati | NON IMPLEMENTATO: coding v5 pretende verifier e il backend verifica dopo ogni risposta Coding |
| Review/provenienza | baseline pre-task, diff attribuito, user changes preservate e revert sicuro | Non verificato |
| Editor | apertura/edit/save atomico/dirty/reopen | Monaco presente; ricerca/tab/diagnostica non complete |
| Terminale | PTY, input interattivo, interrupt, cwd, exit code | Non verificato/assente |
| Runtime/Activity | valore con source, timestamp, units e unknown handling | metriche non integrate nella UI |
| Persistenza/resume | restart con stato esatto, schema migration e no replay non sicuro | localStorage sì; resume UI assente; CLI resume disponibile |
| Approval | correlation ID, one-shot allow/deny, timeout, stale response reject, UI update | approval base e timeout 30s; manca acceptance race/timeout e UX fully integrated |
| MCP discovery/use | registry UI validato, server/tool effettivi, launch/call policy, deny/no side effect, native production | config JSON manuale e backend fake tests; UI native production E2E assente |
| Settings/shortcuts/a11y | persistenza validate, keyboard/focus/contrast/responsive | Parziale |
| Build nativa production | `forge-studio.exe` + `forge.exe` + Gemma locale, no fixture | build disponibile; smoke precedente fixture |
| TaskFlow | requisiti pass con test/build isolati e zero continuation prompt | Non ancora eseguito; sandbox bloccante |
| Continuity/false completion | checkpoint/duplicate proof + verifier per requisito | Non ancora verificato end-to-end |
| PAIT/sandbox | audit documentato + containment negative suite | Aperto/bloccante per esecuzione codice generato |

## 5. Telemetria e definizioni di conteggio

Il report deve definire prima del run:

- **Tool call:** evento tool invocato dal modello, inclusi rifiutati; distinguere completati, falliti, negati e MCP.
- **File letti/modificati:** path unici e operazioni; contare separatamente tentativi, riusciti e modifiche finali; distinguere file preesistenti e file nuovi.
- **Turno modello:** una completion request effettivamente inviata e una risposta ricevuta, con retry di trasporto distinto da iterazione agentica.
- **Intervento umano:** prompt addizionale, approval/deny, stop/restart, cambio configurazione. Le approval previste sono interventi e vanno conteggiate ma non sono prompt di continuazione.
- **Recovery:** azione di harness attivata dopo errore/verifier, distinta da continuazione spontanea del modello.
- **Loop:** stesso tool+argomenti/path ripetuti oltre soglia preregistrata, o nessun progresso osservabile entro N iterazioni; soglia definita prima del run.
- **Tempo:** wall-clock end-to-end, tempo inferenza, attesa approval, esecuzione tool e verifier separati.
- **Token:** valori API `prompt_tokens`/`completion_tokens` o dichiarati da runtime, senza conversioni stimate; indicare quando mancano.
- **VRAM:** fonte, frequenza di campionamento, valore per processo vs sistema; polling 500 ms può perdere picchi brevi. Nessun “massimo” se non campionato adeguatamente.

## 6. Verdetto PASS / PARTIAL / FAIL

### PASS — solo se tutte le condizioni

1. TaskFlow completo secondo checklist preregistrata; test automatici generati eseguiti in sandbox valida e passano, build/typecheck passano, risultati riproducibili.
2. Unico prompt iniziale del task, zero prompt manuali di continuazione; FORGE usa modello/runtime/harness locali verificati.
3. Verifier indipendente conferma ogni criterio e non si basa su dichiarazione del modello.
4. UI nativa usa backend `forge.exe` di produzione per i flussi provati; nessun fixture/mock nella prova di accettazione.
5. Resume/cancel, approvazioni e MCP soddisfano i criteri testati senza duplicazioni o azioni non autorizzate.
6. Nessun finding critico aperto per la configurazione effettivamente usata; sandbox passa test negativi; modello/template/licenza hanno esito documentato accettabile.

### PARTIAL

- Il prodotto o il task mostra evidenze reali ma uno o più criteri richiesti sono bloccati/non supportati, oppure sandbox/audit/native integration non sono conclusi. Specificare ogni blocco e non descrivere il task come completato.

### FAIL

- Modello cloud/fixture sostituisce backend, output generato eseguito senza sandbox, verifier non rileva criteri mancanti, tool call non autorizzata, corruzione/perdita stato, evidenza non riproducibile o report che dichiara successo senza prove.

## 7. Ordine consigliato e dipendenze critiche

1. F0 baseline e inventario → F1 audit modello/licenze e fattibilità sandbox. Non eseguire output generato finché G1 non passa.
2. F2 contratto JSONL e policy → implementare prima routing continuo e semantica `assistant_final`/mutazione/verifier; i test pure-chat senza modello sono unitari e non generativi.
3. Completare F2 store/resume/provenance e definire la permission matrix; quindi implementare F7-Skill/MCP registry sul contratto stabile. La UI non deve precedere il contratto reale di capability e correlazione.
4. F3 shell → F4 navigazione → F5 conversazione unificata → F6 review/editor/terminal/runtime. Ogni schermata si collega solo ad API coperte da test integrazione.
5. F8 verifier e harness → F9 smoke nativo production + prove Gemma solo se G1 è PASS → F10 release/report.
6. Se G1 non passa: completare comunque conversazione locale non esecutiva, persistenza sicura, read/edit con limiti dichiarati e integrazioni di sola lettura. Disabilitare invocazione di comandi e test generati; non simulare successo e non dichiarare acceptance completa.

### Mappa implementativa concreta (spina dorsale)

1. **Protocollo e lifecycle:** `docs/protocols/studio-session-v1.md` (nuovo); evolvere `agent/src/main.c`, `agent/src/coding_agent.h`, `agent/src/coding_agent.c`, `agent/src/agent_loop.h/.c` con `session_id`, `run_id`, `turn_id`, `schema_version`, transcript/context espliciti e stati terminali. Mantenere parser v0/v1 backward-compatible durante la migrazione; test parser e state machine.
2. **Store:** nuovo `studio/src-tauri/src/session_store.rs` con SQLite locale, transazioni/migrazioni e record append-only; nuovi Tauri commands in `studio/src-tauri/src/main.rs`. UI usa `studio/src/state/session-repository.ts` (nuovo) e smette di affidare transcript/config sessione solo a `localStorage`; migrazione non distruttiva con backup/rollback.
3. **Resume:** aggiungere API bridge per listare/validare/riprendere checkpoint; collegare checkpoint a sessione, task hash e workspace canonico. In UI `studio/src/session-model.mjs` e moduli sessione mostrano resume safe, ripresa parziale, solo cronologia o non riprendibile; mai riavvio automatico di una tool call incerta.
4. **Context/memory:** nuovo `studio/src-tauri/src/memory_store.rs` e repository UI; memory scope utente/progetto, CRUD e provenance. Ogni turno costruisce context dal transcript compattato + tail raw + istruzioni + skill selezionata + memory pertinente; il transcript completo resta immutato ed esportabile. Aggiungere `/context`, `/compact` e `/memory` solo quando ciascuno ha implementazione reale.
5. **Instructions/skills:** nuovo resolver registry in `agent/src/` (manifest/hash/origine/precedenza/limiti) con test isolati. Discover `AGENTS.md`, `CLAUDE.md` secondo policy configurata, file Copilot e skills in `.agents/skills`, `.claude/skills`, `.opencode/skills`; roots globali opzionali in settings. Indicizzare description e caricare body per skill solo quando pertinente; nested instructions path-scoped lazy. Nessun contenuto trovato può alterare permessi o system policy.
6. **UI e diagnosi:** scomporre `studio/src/main.tsx` in shell/session/conversation/activity/review/settings; stessa sessione per follow-up, picker con preview/search/rename/archive/branch solo se supportati, Normal/Verbose per eventi, status model/runtime, retry e conservazione draft su errore. `studio/src-tauri/src/main.rs` registra exit code, Windows exception/termination kind, runtime health e log correlati senza segreti.
7. **Launcher/package:** aggiornare `scripts/launch/forge-local.ps1`, `scripts/package-forge-studio.ps1`, `studio/README.md` e acceptance report affinché launcher verifichi runtime `/health`, Studio non mandi prompt offline, il bundle includa la build corrispondente al sorgente e i test distinguano unit, integration, native e Gemma acceptance.

### Prima release che merita di essere usata

La prima release utile deve superare almeno M1 + store/transcript di M3 + project instruction loading e preflight runtime: una sola sessione conversa su più turni, riapre con la cronologia corretta, ricorda il contesto precedente, mostra le istruzioni effettivamente caricate, conserva l'input se il runtime è offline e non attribuisce successo a processi anomali. Skill/MCP automatici, memory auto-write, compaction e resume checkpoint arrivano in milestone successivi con test dedicati; finché non ci sono restano visibilmente “non disponibile”, non simulati.

### Milestone eseguibili e criterio di uscita

| Milestone | Dipendenze | Consegna | Uscita verificabile |
|---|---|---|---|
| M0 — baseline del prodotto | nessuna | inventario corrente, capability matrix, baseline/hash, esempi di conversazione e decisioni UX | ogni requisito ha sorgente, stato, owner, test e gate; nessuna modifica preesistente persa |
| M1 — conversazione agentica unica | M0, schema protocollo F2 | rimuovere toggle chat/coding; tool loop disponibile ma non obbligatorio; prompt/UI messaggi puliti; verifier non più legato a ogni final | suite “CI SEI”, spiegazione, read-only, richiesta di modifica e richiesta esplicita di test; zero tool/verifier nei primi due casi; nessun falso `completed` |
| M2 — istruzioni, skill e memory | M1, policy F7 | registry di instruction/skill, memory separata e ispezionabile, context sources UI, MCP lazy | AGENTS path-scoped; skill pertinente caricata on-demand; memoria isolata/modificabile; input ostile non eleva privilegi |
| M3 — sessioni, transcript e ripresa | F2 store/provenance | store locale transazionale, ID stabili session/run/turn, transcript completo, summary/compaction, checkpoint correlate, resume/interrupt/cancel | restart e follow-up conservano contesto; migrazioni testate; zero replay side effect; corruption recovery esplicita |
| M4 — workspace review sicura | M3, G1 per operazioni esecutive | diff Git/non-Git, baseline, provenance, editor/save atomico, ricerca file, approval chiara | changes preesistenti non attribuite a FORGE; file diff corrisponde al ledger; restore/reopen e dirty-state testati |
| M5 — MCP/approval/strumenti pronti | M2, G1 per tool mutativi | impostazioni, registry, grant per capability, launch/call lifecycle, timeout e log redatti | test nativi su backend production: allow/deny, scadenza, cancel e correlazione; zero azioni senza grant |
| M6 — verifier e runtime isolato | G1 obbligatorio | sandbox/runner, profili test, verifier indipendente, requisiti atomici | suite containment negativa verde; test generati solo dentro sandbox; finali prematuri rifiutati |
| M7 — release d'uso quotidiano | M1–M5; M6 per task execution | installer/bundle pulito, onboarding, istruzioni semplici, diagnostica/rollback | installazione fuori checkout, launcher e avvio app verificati; tutti i flussi dichiarati reali passano UI nativa + backend production |
| M8 — acceptance completa | M0–M7, G1 | TaskFlow, continuità, false completion e report firmato/hashato | tutti i criteri preregistrati passano; nessun mock; esito e limiti riproducibili |

Non si dichiara “100%” saltando un milestone. Se il gate sandbox/audit resta bloccato, la consegna può diventare utile per chat e workspace non esecutivo, ma M6/M8 restano BLOCKED e non si eseguono test generati sull'host.

## 8. Deliverable finali

- `studio/src-tauri/target/release/forge-studio.exe` versione ridisegnata e avviabile con il backend FORGE di produzione.
- UI completa per le funzioni che superano i gate; non supportate disabilitate e dichiarate.
- Test/unit/integration/native e runner sandbox con istruzioni riproducibili.
- `artifacts/forge-e2e-acceptance.md` con verdetto, prove grezze referenziate, checklist e tutti i limiti.
- `artifacts/m0-status.md` aggiornato con stato residuo; `LICENSES/NOTICE.md` aggiornato se si riusa codice/asset.
- Nessun commit automatico e nessuna sovrascrittura di modifiche preesistenti.
