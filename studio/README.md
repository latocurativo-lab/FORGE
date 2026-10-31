# FORGE Studio

App desktop locale nello stack Tauri 2, React e Monaco. Include elenco progetti, ricerca e raggruppamento sessioni, titoli modificabili, tab locali, editor e salvataggio con controllo delle modifiche concorrenti. Modalità **Conversazione** (predefinita) non espone strumenti/MCP/verifier; **Coding** mantiene tool loop, verifier e approval. In chat il centro mostra solo i messaggi utente e le risposte finali; attività tecniche e approvazioni restano nel pannello laterale. Progetti, impostazioni e cronologia (massimo 50 sessioni, eventi limitati) sono conservati nel local storage della WebView: non è ancora un archivio transazionale e le sessioni non sono riprese/rieseguite dopo la chiusura dell'app.

## Avvio e verifica

Prerequisiti Windows: Node.js, Rust/Cargo MSVC, toolchain MSVC C++ e WebView2. Rust è fissato a `1.99.1` in `rust-toolchain.toml`; `src-tauri/Cargo.lock` blocca le dipendenze Rust. Rustup può essere installato nello spazio utente seguendo la guida ufficiale <https://rustup.rs/> (non servono privilegi elevati né modifiche al PATH di sistema). Per una shell senza PATH già aggiornato: `$env:PATH = "$env:USERPROFILE\.cargo\bin;$env:PATH"`.

```powershell
cd studio
npm ci
npm run build
npm run test
npm run tauri -- build
# Verifiche Rust con la toolchain fissata:
cargo check --locked --manifest-path src-tauri/Cargo.toml
cargo test --locked --manifest-path src-tauri/Cargo.toml
npm run tauri -- dev
```


`npm run dev` da solo serve solo il frontend: le operazioni native richiedono Tauri. La build desktop richiede anche accesso alle dipendenze Cargo. Non sono stati aggiunti permessi generici filesystem al frontend; i comandi nativi limitano gli accessi alla cartella selezionata e ignorano i collegamenti fuori da essa. Il controllo non costituisce una sandbox contro modifiche concorrenti ai collegamenti sul filesystem.

## Bridge agente JSONL

Il comando Tauri `start_agent` avvia `forge.exe` senza argomenti e invia una riga JSONL `{ "type": "start", "task": "...", "workspace": "...", "allow_exe": ["C:\\...\\test.exe"], "verifier": { "executable": "C:\\...\\test.exe", "args": "..." }, "mcp_servers": [] }`. Le impostazioni verifier/allowlist e la configurazione MCP sono persistite nel local storage; per avviare task di coding serve un eseguibile verifier assoluto incluso nella allowlist. Non viene aggiunto automaticamente alcun eseguibile alla allowlist.

Per MCP si può inserire un array JSON opzionale fino a 8 server stdio, per esempio `[{"id":"local","executable":"C:\\\\tools\\\\server.exe","args":[],"allowlist":["C:\\\\tools\\\\server.exe"],"timeout_ms":10000}]`. Il percorso executable deve essere assoluto e ripetuto nella allowlist del server. FORGE chiede conferma separata prima del launch (con checkbox di conferma esecuzione esatta) e prima di ogni chiamata; ogni chiamata è considerata potenzialmente mutativa. Descrizioni del server non vengono passate al modello, i risultati sono dati non attendibili. Il process Job Object è terminato a fine sessione; questo non è una sandbox e l'eventuale server mantiene i permessi dell'utente.

Le righe JSON lette da stdout sono pubblicate come evento Tauri `agent-event` (`{ sessionId, event }`) e mostrate nella cronologia locale; righe non JSON sono mostrate come output testuale. stderr viene mostrato come evento `stderr`. Un evento `approval_required` espone i dettagli della richiesta (tool, path/eseguibile, args, call ID, capability, mutation e `request_id`) e attende una scelta esplicita. `approve_agent` scrive `{ "type": "approval", "request_id": "...", "approved": true|false }` su stdin seguito da newline e flush. Esecuzioni dirette e launch MCP richiedono anche un'ulteriore conferma esplicita separata nell'interfaccia; approvare non modifica l'allowlist. `cancel_agent` invia `{ "type": "cancel" }` seguito da newline e flush; l'esito dell'annullamento dipende dal supporto del processo.

Ricerca eseguibile: percorso scelto in **Seleziona forge.exe…** (salvato nella configurazione Tauri locale), `FORGE_EXE`, accanto a Studio, cartella `resources`, `build-agent/Release/forge.exe` del checkout oppure PATH. Il build di verifica `build-agent-verify` non viene scelto automaticamente. Lo stato mostra path e sorgente; se manca, l'avvio viene rifiutato, senza fallback simulato. Il backend CLI implementa il protocollo JSONL qui sopra; stdout resta riservato agli eventi JSONL e i diagnostici vengono scritti su stderr.

Il salvataggio verifica che il contenuto su disco coincida con quello aperto, ma non è una transazione atomica. File binari/non UTF-8 non sono supportati. Le modifiche non salvate richiedono conferma prima di cambiare file o cartella; la chiusura della finestra con dirty state in WebView2 resta da verificare. Ricerca/rename sessioni e schede sono funzionalità UI locali, non resume backend.

## Pacchetto portabile locale

Da PowerShell nella root del repository: `./scripts/package-forge-studio.ps1`. Crea backend Release in una directory CMake separata, esegue CTest, test Studio/Rust e build Tauri, poi produce `artifacts/forge-studio-portable/` con `forge-studio.exe`, `forge.exe` affiancati e hash SHA-256 in `SHA256SUMS.json`. Rifiuta di sovrascrivere una directory d'uscita esistente. Bundle per test locale: non include Gemma e non certifica sandbox/licenze/acceptance.

Le richieste di approval attendono al massimo 30 secondi; alla scadenza l'azione viene negata e l'evento esplicito compare nel pannello Attività. Conversazione e coding usano comunque il modello locale, quindi la latenza dipende dal runtime e dalla generazione.

Per avviare runtime locale e Studio insieme dal checkout: `./scripts/launch/forge-local.ps1 -Mode Studio -StudioPath 'C:\percorso\forge-studio-portable\forge-studio.exe'`. Lo script usa `models/MODEL.lock`, controlla hash/dimensione e avvia llama-server su loopback `127.0.0.1:18932`; chiudere Studio termina solo il runtime avviato da quello script. Integrità/hash non costituiscono un giudizio di sicurezza.

## Smoke nativo Windows

Il fixture `tests/native-bridge-fixture.c` è un backend di test esplicito (non viene incluso nella build di produzione). Da un Developer PowerShell x64 di Visual Studio:

```powershell
cl /nologo /O2 /MT /W4 /Fe:"$env:TEMP\forge.exe" tests/native-bridge-fixture.c
$env:FORGE_EXE = "$env:TEMP\forge.exe"
& src-tauri\target\release\forge-studio.exe
```

Aprire una cartella temporanea con `safe.txt`, modificarla e salvarla; poi avviare una sessione dal pannello agente. Il fixture emette subito un evento prima che `start_agent` abbia necessariamente restituito l'ID (controllo della race), richiede una approvazione e un rifiuto e infine attende `cancel`. Gli eventi `fixture_approval_received` / `fixture_cancel_received`, l'uscita del processo e il contenuto aggiornato di `safe.txt` verificano il bridge stdin/stdout JSONL e il salvataggio reale. Il verifier e la sua allowlist devono contenere lo stesso percorso assoluto; il fixture non esegue i comandi verifier né le richieste simulate.

Verifiche aggiornate nel log di esecuzione e nel report acceptance. La build portabile avvia Studio con `forge.exe` affiancato e non richiede `PATH` di sviluppo. Il backend contatta llama-server solo su loopback `127.0.0.1:18932`; per avviare insieme runtime e Studio dal checkout usare `scripts/launch/forge-local.ps1 -Mode Studio -StudioPath <percorso-alla-build-portabile>`. Questo comando verifica hash/dimensione modello secondo il lock, avvia llama-server locale e lo arresta quando Studio viene chiuso; integrità/hash non equivalgono a un giudizio di sicurezza.
