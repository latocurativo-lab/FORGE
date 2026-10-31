# Follow-up sessione agente — verifica v14 (2026-10-29)

- Segnalazione: i messaggi di follow-up venivano registrati nella conversazione ma non ricevevano risposta.
- Il transcript persistito della sessione `ciao io sono manuel` contiene 5 turni replayabili: utente, assistente, poi tre messaggi utente. In v13 il backend terminava prima dell'evento `start`, con stato logico `EXECUTING` e codice 1.
- Con il backend v14, lo stesso transcript letto in sola lettura e lo stesso follow-up `chi sono?` producono `start`, `generation_started`, una risposta coerente (`Sei Manuel, come mi hai appena detto.`), `exit COMPLETED`, codice di processo 0 e stderr vuoto.
- Pacchetto v14: `artifacts/forge-studio-portable-v14`. SHA-256 Studio `3F1E139B48E5F17BB57A11EF00B9CA81D0BB72EE8F7A0B8D4760E1807609EB28`; backend `ABC232AAE954AAD06DA3E1E2ECBD4C767008F378B89077E82122C695E5ED158D`.
- Verifiche pacchetto: frontend 28/28, Rust 8/8, CTest 10/10, build PASS. Smoke diretto usa il backend reale e il transcript esatto; non invia tool call né modifica il transcript.
- v14 è stata riaperta: processo Tauri rispondente, runtime HTTP health 200 e controlli UI accessibili presenti. La UI non è stata pilotata; quindi il send-through Tauri non è certificato end-to-end.
- Nota: il rapporto di dettaglio precedente nel registro generale contiene caratteri di controllo introdotti da quoting errato del comando PowerShell; usare questo file per la verifica v14.
