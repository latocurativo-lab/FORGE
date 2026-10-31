# Piano di accettazione TaskFlow — checklist atomica + verifier esterno (design)

**Data:** 2026-10-29 · **Stato:** PIANO, pre-registrato PRIMA del run (superplan §"Verifier indipendente e acceptance — P0 prima del TaskFlow"). **Nessun run eseguito.** Gate bloccanti correnti: sandbox (artifacts/forge-sandbox-design.md) e firma modello/licenza (artifacts/forge-gates-model-license.md).

## 1. Congelamento del task (pre-run)

Il task TaskFlow concordato va riportato **letteralmente, una volta sola**, nel composer di FORGE Studio nativa collegata al backend production `forge.exe`. Il testo del prompt va congelato in `artifacts/taskflow-prompt.txt` (hash SHA-256 registrato) PRIMA del run. Nessun prompt di continuazione manuale; le approval previste da policy sono interventi conteggiati ma non prompt di continuazione (superplan §5 "Intervento umano").

## 2. Checklist atomica dei requisiti (derivata dal brief citato nei piani)

Deriva da `docs/plans/forge-studio-redesign-e2e-superplan.md` (§Fase 8 punto 2: "CRUD, toggle completamento, priorità, filtri, persistenza locale, TypeScript, struttura progetto, UI responsive, test automatici, documentazione") + criteri osservabili aggiuntivi preregistrati. Ogni criterio: ID, definizione misurabile, fonte di prova, criterio di PASS.

| ID | Requisito | Criterio misurabile (PASS quando) | Prova raccolta da |
|---|---|---|---|
| TF-01 | Persistenza attività | File sorgente di task/modello esiste nel workspace prodotto, con hash registrato | verifier legge file, calcola SHA-256, lo archivia |
| TF-02 | Creazione task (C di CRUD) | chiamata/azione che crea un task produce record verificabile su disco (es. JSON/DB del prodotto, non solo memoria) in formato documentato dal README del progetto generato | verifier ispeziona l'artefatto, conta >= 1 record |
| TF-03 | Lettura del elenco (R) | l'elenco delle attività creato è in persistenza: la struttura dati contiene tutti i record creati | verifier parsa l'artefatto persistente, conta record == creati |
| TF-04 | Aggiornamento (U) | modifica titolo/stato persiste su disco dopo il ciclo di creazione | verifier diff pre/post hash del file persistente |
| TF-05 | Cancellazione (D) | eliminazione rimuove il record in persistenza | verifier ricontrolla il file; record assente |
| TF-06 | Toggle completamento | campo done/completed flippa su disco e resta | inspector campo pre/post |
| TF-07 | Priorità | campo priority accetta/setato con valori enumerati validi | inspector valori su disco |
| TF-08 | Filtri | criterio/funzione di filtro filtra l'elenco rispettando >= 2 stati/priorità | test dalla suite generata del progetto o verifier su struttura |
| TF-09 | Persistenza locale (reload) | dopo restart/app ricarica, i task restano | secondo avvio del prodotto + relazione su disco |
| TF-10 | TypeScript | progetto compila con `tsc --noEmit`/typecheck exit 0 **dentro la sandbox approvata** | verifier esegue typecheck in runner, archivia exit+stdout |
| TF-11 | Struttura progetto | struttura dichiarata nel README del progetto generato esiste su disco | verifier confronta albero atteso vs reale |
| TF-12 | UI responsive | il progetto documenta/implementa breakpoints; oppure unit/integration test di layout passano | test della suite generata in sandbox; proof exit 0 |
| TF-13 | Test automatici generati | suite di test del progetto generato esegue in **sandbox approvata** exit 0 | runner archivia stdout/stderr/exit |
| TF-14 | Documentazione | README del progetto con avvio + requisiti | verifier legge README, presenza sezioni minime |
| TF-15 | Stati vuoti/errori | gestione di lista vuota/errore documentata o testata | test della suite in sandbox |
| TF-16 | Build | build del progetto generato exit 0 **in sandbox** | runner archivia exit/stdout |

**Regole dal run (governance, superplan §5):**
- Zero prompt di continuazione. Un unico prompt iniziale congelato.
- Ledger eventi JSONL del run in copia esatta archiviata (eventi: start, tool_request/result, approval, assistant_final, verification_*, exit).
- **File ledger** (da forge.exe/eventi): ogni write/read registrato con path, hash pre/post, operation ID; il verifier ricalcola gli hash post per tutti i file toccati e li confronta con quelli del ledger.
- Metriche (turni, token, wall-time, VRAM) solo se sorgente reale esposta: altrimenti campo `unknown` esplicito, mai stimate.

## 3. Continuità (estensione Gate G2-Continuity)
- Sessione con task TaskFlow, >= 3 turni correlati nella stessa sessione (es. "aggiungi task X" → "segna X fatto" → "quanti task totali?"), verifica che il modello ricava l'informazione dai turni precedenti e non ri-ferma sola la domanda.
- Al termine: riavvio Studio; la cronologia messo-on è quella salvata (mode interrupted è distinguibile da resume). **Test di resume automatico NON richiesto in questo piano** (fuori scope: registrato come mancante in v8).
- Prova: transcript JSONL + risposta del modello che cita correttamente gli artefatti dei turni precedenti; hash transcript archiviato.

## 4. False conclusioni (recovery + verifier)
- Modalità controllata: prima del run il verifier registra un **criterio pre-rotto** (task noto incompleto atteso, senza sabotare runtime/file: questo serve a dimostrare che un requisito mancante è rilevato).
- Verificare che una dichiarazione `assistant_final`/"task done" del modello **non** venga presa per completamento: il verifier dichiara blocked/partial con motivo. Il test PASSA quando il verifier **frena** la conclusione prematura (+ dimostra che l'esito è blocked/partial, non "completato") — il mismarking del modello non trasforma la conclusione.
- Recovery: al termine con un errore inserito, il secondo tentativo di run (recovery del harness, distinta dall'iniziativa spontanea del modello) deve utilizzare checkpoint/approval invariate e non ripetere side effect non idempotenti (superplan §5 "Recovery").

## 5. Approvals
- Ogni approval: evento correlato per `tool_call_id`; deny/timeout non possono trasformarsi in allow (regola superplan Approval).
- Conteggio approval nel run + tempi di attesa; le policy pre-deliberate si firmano prima del run nel `artifacts/taskflow-run-manifest.md`.
- Un approval scaduta/stale non è ri-approvabile (UI aggiornata dalla v5, da verificare in E2E).

## 6. Verifier esterno (design)
Strumento `scripts/acceptance/verify-taskflow.ps1` (da implementare DOPO il gate sandbox; in questo percorso non eseguito), che:
1. Legge `artifacts/taskflow-run-manifest.md` con le soglie preregistrate (hash prompt, lista criteri attivi).
2. Ispeziona il workspace usa-e-getta senza eseguire il codice generato (read-only): TF-01…TF-09, TF-11, TF-14.
3. Pipeline di ESECUZIONE in sandbox approvata: typecheck (TF-10), test suite (TF-13/15), build (TF-16) — **solo se il gate sandbox è PASS**.
4. Ogni criterio → record motivazione + prova + hash: `artifacts/taskflow-verifier-report.json` con `schema_version`, input hashes, per-criterio PASS/FAIL/UNKNOWN, firma (SHA-256 del report).
5. Niente fallback a dichiarazioni del modello: "fatto" del modello è solo evento, mai condizione di PASS (superplan §6).

## 7. Mappatura gate residuale / ordine di sblocco

| Gate | Dipende da | Stato ora |
|---|---|---|
| Sandbox (S1–S5 di forge-sandbox-design.md) | Adozione utente engine (Docker/WSL/WinSandbox) | BLOCCANTE per TF-10/13/15/16 e per esecuzione di codice generato |
| Modello + licenza (forge-gates-model-license.md D1–D5) | Firma utente su finding+licenza+NOTICE | DECISO=PENDENTE (prove documentate) |
| Studio nativo + backend production E2E (forge.exe) | risolvere sandbox prima | non verificato in un run TaskFlow (bundle v8 build/packaging PASS, non acceptance) |
| Verifier esterno scaffold | questo piano + engine sandbox | NON implementato (solo design) |

**Ordine di sblocco quindi:** firma D1–D3 (model-license) → S1–S4 sandbox → implementare verifier §6 → run TaskFlow (una volta) → continuità §3 → false conclusioni §4 → report artefatti finali (forge-e2e-acceptance.md aggiornato).

## 8. Definizioni fine (PASS/PARTIAL/FAIL)
Presi dal superplan §6 — si citano, non si riscrivono: PASS solo con **tutti** i criteri TF-* preregistrati + sandbox + modello/licenza approvate + zero continuazione + UI nativa production; PARTIAL con blocchi constatati; FAIL se si esegue codice generato senza sandbox, dittature verifiche sans prova, corruzione/perdita di stato o evidenza non riproducibile.
