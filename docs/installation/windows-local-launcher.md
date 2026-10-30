# FORGE locale su Windows

> **Aggiornamento (ottobre 2026)** — il serving del modello è gestito ora da
> `scripts/launch/start-qwen-local.ps1` (Qwen3.6-35B-A3B, variante
> `Abliterated`/`Base`, contesto 65536, KV q8_0, offload MoE). Per Studio/CLI
> resta valido quanto scritto sotto; i riferimenti a Gemma 4 12B e
> `-ContextSize 4096 -GpuLayers 99` descrivono la configurazione erede M0 e
> sono attualmente superati dalla ricetta Qwen documentata in `README.md` e
> `paper/forge.md` (§4).

Il launcher `scripts/launch/forge-local.ps1` avvia il runtime GGUF locale e poi
FORGE CLI oppure FORGE Studio. Non installa componenti, non scarica il modello e
non modifica installazioni o configurazioni esterne. Esegue `llama-server` solo
se la porta loopback `127.0.0.1:18932` è libera; se trova un server già attivo,
ne controlla `/health`, lo riusa e non lo termina.

## Prerequisiti

- Windows PowerShell 5.1 o PowerShell 7; script in esecuzione con i normali
  permessi dell'utente (non richiede elevazione).
- Modello già presente in `models/gemma-4-12b-it-Q4_K_M.gguf`, senza download
  automatico. `models/MODEL.lock` documenta origine, licenza, dimensione e SHA-256.
- `llama-server.exe` già compilato in
  `runtime/upstream/llama.cpp/build/bin/Release/llama-server.exe`.
- `forge.exe` già compilato in `build-agent/Release/forge.exe` per CLI.
- Per Studio, eseguibile Tauri già compilato; il percorso predefinito è
  `studio/src-tauri/target/release/forge-studio.exe`. Usare `-StudioPath` se il
  nome/percorso prodotto dalla toolchain locale differisce.

Il server viene vincolato a loopback e usa porta 18932, template Jinja e
reasoning disabilitato. I default CUDA/context sono conservativi e configurabili:
99 GPU layers, context 4096, 4 thread e 1 slot parallelo. Adattarli alla memoria
disponibile. Il launcher attende `/health` fino a 180 secondi e poi attende la
chiusura dell'app per terminare esclusivamente il processo server avviato da sé.

Stato al completamento del percorso Tauri: modello e `llama-server.exe` sono
presenti; Studio viene prodotto dai comandi sottostanti. La disponibilità di
`build-agent/Release/forge.exe` dipende dalla build C corrente; se manca, il
bridge Studio rifiuta esplicitamente l'avvio dell'agente.

## Build manuale

Le build sono esplicite e separate dal launcher. Seguire la documentazione del
fork llama.cpp presente in `runtime/`; non è compreso alcun bootstrap o download
implicito di dipendenze. Per l'agente C:

```powershell
cmake -S agent -B build-agent -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent --config Release
ctest --test-dir build-agent -C Release --output-on-failure
```

Per preparare Studio servono Node.js, Rust/Cargo MSVC, toolchain MSVC C++ e
WebView2, secondo Tauri 2. Rust è fissato a `1.99.1` da
`studio/rust-toolchain.toml`; Cargo risolve le dipendenze dal
`studio/src-tauri/Cargo.lock`. Installare Rustup solo dallo schema ufficiale
<https://rustup.rs/> e nello spazio utente, senza elevation o PATH di sistema.
Dalla directory `studio`:

```powershell
npm ci
npm run build
npm run test
cargo check --locked --manifest-path src-tauri/Cargo.toml
cargo test --locked --manifest-path src-tauri/Cargo.toml
npm run tauri -- build
```

`bundle.active: false` produce una build desktop senza installer. Il percorso
nativo è stato compilato su Windows con Rust/Cargo `1.99.1`, target MSVC, Visual
Studio Build Tools e WebView2; il binario risultante è
`studio/src-tauri/target/release/forge-studio.exe`. Il test interattivo nativo ha
verificato workspace, save, eventi precoci e protocollo JSONL approval/reject/
cancel con `studio/tests/native-bridge-fixture.c`. Per ripetere lo smoke, vedere
`studio/README.md`.

## Uso

Dalla root del repository, prima visualizzare il piano senza effetti collaterali:

```powershell
.\scripts\launch\forge-local.ps1 -WhatIf
```

Avvio automatico (Studio se l'eseguibile esiste, altrimenti CLI). Se viene
selezionata CLI, `-ForgeArguments` è obbligatorio: senza argomenti `forge.exe`
entra nel protocollo JSONL da stdin e il launcher si ferma prima di avviare il
server.

```powershell
.\scripts\launch\forge-local.ps1
```

Selezione esplicita e override delle risorse:

```powershell
.\scripts\launch\forge-local.ps1 -Mode CLI -ContextSize 4096 -GpuLayers 99 -Threads 4
.\scripts\launch\forge-local.ps1 -Mode Studio -StudioPath 'D:\apps\FORGE Studio.exe'
```

Gli argomenti CLI vanno passati esplicitamente con `-ForgeArguments`, includendo
task, workspace, verifier e allowlist. Esempio:

```powershell
.\scripts\launch\forge-local.ps1 -Mode CLI -ForgeArguments @(
  'run', '--task', 'Implement the change', '--workspace', 'C:\work\fixture',
  '--verify-exe', 'C:\tools\ctest.exe', '--verify-args', '--test-dir build --output-on-failure',
  '--allow-exe', 'C:\tools\ctest.exe'
)
```

`-WhatIf` stampa percorsi e argomenti previsti senza hash, probe di rete o avvio
di processi. Il normale `-Confirm`/`-WhatIf` di `ShouldProcess` protegge anche
l'avvio dei processi. Errori e diagnostica vengono mostrati in console; il
launcher non scrive file di log e non richiede segreti.

## Integrità, licenze e limiti di sicurezza

Prima dell'avvio viene verificata la dimensione e SHA-256 del modello rispetto ai
valori registrati in `models/MODEL.lock`, e viene mostrata la fonte dichiarata.
Questi controlli verificano corrispondenza con i metadati locali, **non**
certificano che il modello sia sicuro, affidabile o privo di comportamenti
indesiderati. La provenienza non viene autenticata dal launcher. Rivedere le
licenze del modello/base model e dei componenti runtime separatamente prima di
redistribuirli o usarli; il lock riporta la licenza dichiarata dal repository
Unsloth e il modello base Google Gemma 4. Le licenze non vengono determinate dal
checksum.

Il bind loopback riduce l'esposizione di rete ma non è un sandbox. Runtime,
agente e Studio ereditano i permessi dell'utente; l'harness non rende sicuri gli
output del modello né isola file o processi. Non eseguire codice o comandi
proposti dal modello senza revisione e approvazione. Se la porta è occupata da un
server non sano o non verificabile, lo script si ferma senza terminarlo.
