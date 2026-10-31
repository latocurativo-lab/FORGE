# Sandbox per codice/test generati — design + strumento (nessuna esecuzione)

**Data:** 2026-10-29 · **Stato:** SOLO DESIGN + SCHELETRO STRUMENTALE. **Nessun test di contenimento è stato eseguito in questa sessione.** Niente qui autorizza l'esecuzione di output generati: la suite negativa va fatta dopo approvazione utente.

## 1. Fatti macchina (osservati oggi, comandi read-only)

| Fatto | Dettaglio |
|---|---|
| OS | Windows 11 Pro, build **26300.9711** (`HKLM:\…CurrentVersion`: `Professional`) |
| Windows Sandbox (`WindowsSandbox.exe`) | **Assente**: né in `%LOCALAPPDATA%\Microsoft\WindowsApps` né nel PATH |
| Optional feature `Containers-DisposableClientVM` (payload Windows Sandbox) | Pacchetti CBS presenti in servicing store con `CurrentState=64`; **lettura non elevata**: 64 è coerente con "non installata" ma la conferma definitiva richiede `Get-WindowsOptionalFeature -Online` elevato (passi già risolti mancante richiesto: **elevazione utente**) |
| Docker Desktop | Client **29.5.3** (windows/amd64, context `desktop-linux`) installato e nel PATH; **daemon non in esecuzione** alla verifica (`npipe dockerDesktopLinuxEngine` assente) |
| WSL2 | Presente. Distros: `docker-desktop` (stopped), `magenta-rt` (stopped), **`kali-linux` (running)**, versione predefinita WSL 2 |

Nota onestità: `CurrentState=64` non è una prova di "feature disabilitata" — coerente con quanto già documentato in `artifacts/forge-e2e-acceptance.md` ("Disponibilità host = sconosciuta"). La differenza oggi: **Docker Desktop + WSL2 sono installati**, quindi esiste un'opzione di isolamento realistica senza installare nulla di nuovo.

## 2. Opzioni valutate (Fase 1 del superplan)

### Opzione A — Container Linux via Docker Desktop (raccomandata, presente)
- I file generati NON vengono montati come bind rw dall'host: il workspace è **copiato dentro** una directory di lavoro del container (mai il contrario), con `--network none`, `--memory`, `--cpus`, `--pids-limit`, cap stdout, `--read-only` root FS, entrypoint fisso, immagine pinata per digest (non `:latest`).
- Isolamento: kernel Linux dedicato in VM (docker-desktop WSL2), quindi containment kernel diverso dall'host; il codice generato non vede l'utente Windows né il FS host.
- **Prerequisiti verificabili dall'utente:** avviare Docker Desktop, verificare `docker version` server + pull immagine; TUTTO da approvare. Oggi non è provata alcuna esecuzione in Docker (daemon spento) ⇒ **opzione disponibile, non provata**.

### Opzione B — Seconda distro WSL2 usa-e-getta ("forge-ink")
- `wsl --import FORGE-SBX <dir> <rootfs>` con distro dedicata e rete disattivabile in policy; lo snapshot = ripartire da distro pulita a ogni run. Isolamento leggermente inferiore a Docker (il mount `/mnt/…` verso i dischi host va disattivato per policy), ma nessun requisito Docker Desktop.
- La distro `kali-linux` in esecuzione è l'ambiente pentest dell'utente: **non usarla come sandbox FORGE** (contiene strumenti e configurazione dell'utente; non è usa-e-getta).

### Opzione C — Windows Sandbox (feature opzionale)
- Richiede: `Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM` **elevato** + reboot + conferma feature abilitata. Isolamento forte e usa-e-getta per design, rete governabile via `.wsb`. Da attivare **solo se l'utente accetta elevazione+reboot**.

### Opzione D — Contenimento on-host (FALLBACK, NON È UNA SANDBOX)

Utente Windows dedicato minimamente privilegiato + **Job Object** (limite CPU/RAM/processi/tempi) + **dir temp usa-e-getta** (ACL), rete disattivata con **regole firewall per-utente/outbound deny**, allowlist eseguibili, conservazione exit/stdout.
- **Da dichiarare nei report:** in base a `docs/plans/*` questo **non si qualifica automaticamente come sandbox** ("Job Object/allowlist/temp dir non sono sandbox", log 2026-10-29). È un confine di riduzione del rischio; se le opzioni A-C sono impossibili, TaskFlow resta PARTIAL con sandbox assenza documentata, e ai sensi del superplan (§6 PASS) **i test generati non vanno eseguiti**.

## 3. Policy minima del runner (quando viene attivato)

Derivata da superplan Fase 1 §"Sandbox per programmi e test generati":

1. **Workspace snapshot usa-e-getta:** copia del workspace in una dir temporanea disposable; **credenziali/dati sensibili host mai montati o copiati**.
2. **Rete di default disattivata** (`--network none` / firewall deny) — attivazioni eccezionali solo con flag utente e log.
3. **Limiti:** memoria, CPU, # processi, tempo wall-clock, dimensione stdout/stderr (cap + taglio con log del taglio).
4. **Filesystem allowlist:** solo la dir di lavoro del container/utente; rifiuto di path con `..`, symlink/junction, reparse points fuori allowlist.
5. **Prove archiviate:** exit code, stdout/stderr cappati, elenco file creati (ledger), tempo, versione immagine/utente/distro — salvati in `artifacts/sandbox-runs/<timestamp>/`.
6. **Cleanup affidabile:** kill del process tree, rimozione container/dir verificata con re-check; la dir di lavoro è usa-e-getta (la checklist negativa la chiama `cleanup`).
7. **Approvazione distinta:** il runner NON è parte del normale `EXEC` per l'agente; è uno strumento separato, invocato manualmente/approvato (per l'integrazione FORGE futura: una approval dedicata distinta dal verifier).

## 4. Suite NEGATIVA di contenimento (SOLO DOPO approvazione utente)

Ogni test = un piccolo payload noto e costante (nel repo `scripts/sandbox/tests/`, da creare al momento dell'approvazione), eseguito nel runner, esito atteso = **la fuga o l'effetto è bloccato** (la scrittura/connessione fallisce o resta confinata):

| # | Test | Payload (descritto, non eseguito) | Criterio superato |
|---|---|---|---|
| N1 | fuga cwd | `Set-Location ..` + tentativo di scrittura file sopra la root di lavoro | file NON creato fuori dalla root sandbox |
| N2 | scrittura fuori | tenta `C:\Users\<utente>\…forge-escape-marker.txt` | file assente + errore permesso/volume |
| N3 | rete | `curl`/`Invoke-WebRequest` a 2 host noti (endpoint esterno + DNS) | fallimento connessione dentro timeout, nessun pacchetto in uscita (log firewall se opz.D) |
| N4 | persistenza | scrive in `~/.bashrc`/schtasks/registro (opz.D) | non persistente al secondo run con workspace pulito |
| N5 | spawn figli | parent lancia figli che tentano di leggere file/variabili host fuori dalla sandbox | figli limitati/uccisi da Job Object/container; nessun accesso |
| N6 | timeout | `sleep 3600` | processo terminato entro `--timeout`, exit loggato |
| N7 | process tree | parent lancia figli CPU-bomb e con traffico di rete | tutti i figli terminati al kill del parent |
| N8 | output enorme | loop `1..1e7` stdout | stdout tagliato al cap, run registra truncation |
| N9 | crash | payload che termina con 0xC0000005/segv | runner sopravvive, registra exit code di crash, cleanup ok |
| N10 | symlink/junction | creare junction fuori dalla dir di lavoro e leggerla | negato/errore; nessuna lettura fuori |
| N11 | cleanup | doppio run: prima run lascia artefatti, seconda run parte pulita | assenza artefatti della prima run |

Superano TaskFlow prep solo se **tutti** i test passano e le prove (log, hash, exit) sono archiviate. Un test fallito = sandbox NON certificata e nessun codice generato eseguito (regola G1/B).

## 5. Strumento: `scripts/sandbox/forge-sandbox-run.ps1` (skeleton)

Creato, **non eseguito**. Comportamento previsto:
- **Default rifiuta:** senza il flag esplicito `-AcceptSandboxNotValidated` (e senza `-Mode` con opzione già verificata) lo script stampa i prerequisiti mancanti ed esce **senza avviare processi, copie o container**.
- `-WhatIf` mostra solo il piano senza effetti; `-ListTests` stampa la suite negativa; `-Mode docker|wsl|winsandbox|hostuser` seleziona l'opzione; parametri espliciti per ogni limite (memoria, CPU, pids, timeout, cap stdout).
- Preflight: verifica che l'engine sia raggiungibile (`docker info`/`wsl -l`), che l'immagine sia pinata per digest, che la dir di lavoro non contenga credenziali, e **fallisce** se i prerequisiti non sono misurati (mai presumere).

Integrazione FORGE (futura): aggiungere a `forge.exe` un profilo di verifier che invoca il runner; la chain approval → run resta separata dal verifier normale (superplan Fase 1 punto 3).

## 6. Passi già risolti vs lavoro da completare

**Fatti qui (nessuna autorizzazione richiesta, nessuna esecuzione):** inventario disponibilità (Docker Desktop presente/spento, WSL2 attivo con kali, Windows Sandbox non installata), design della policy, definizione della suite negativa, skeleton del runner con refusal di default.

**Da fare dopo approvazione utente (con elevazione/azioni dove richiesto):**
1. **S1:** avviare Docker Desktop e verificare `docker version` server (o attivare Windows Sandbox elevando, o creare distro WSL dedica).
2. **S2:** decidere opzione A/B/C/D con firma.
3. **S3:** implementare il runner reale (docker run / wsl -d / Sandbox .wsb / utente+Job Object) e i payload N1–N11 di test.
4. **S4:** eseguire la suite negativa e archiviare prove in `artifacts/sandbox-runs/…`.
5. **S5:** solo dopo S4 superato abilitare l'esecuzione di test generati nel TaskFlow run.
