# Agente unificato e limite token modello — v16 (2026-10-29)

- L'adapter invia `max_tokens: -1` (nessun cap di output deciso da FORGE); la richiesta è stata accettata dal server llama.cpp locale con HTTP 200 e `finish_reason: stop`.
- Il launcher usa `-c 0`: llama.cpp prende il context length dal modello e applica il suo fit alle risorse disponibili, al posto del precedente valore fisso `-c 4096`.
- La UI non espone più la scelta Conversazione/Coding; ogni invio usa lo stesso agente con strumenti disponibili. Saluti e turni solo testuali terminano come normali risposte complete. Le approvazioni/allowlist e la verifica esplicita dei cambiamenti restano protezioni operative.
- Gli eventi `incomplete` (per esaurimento del contesto o altra condizione del runtime) vengono mostrati nella conversazione insieme al testo parziale: non restano più invisibili.
- Bundle `artifacts/forge-studio-portable-v16`; Studio SHA-256 `C3626F1646118393D6D538E0E638900AB99ACF102C86E4189B0F77F8ECB20762`; backend SHA-256 `4D74503A705F983687835367507B93E9469181DF06526161EE63239E68F745CD`.
- Test: frontend 28/28, CTest 10/10, Rust 8/8; build Tauri PASS. Smoke backend dal bundle con modalità unificata e prompt testuale: risposta ricevuta, `exit COMPLETED`, codice processo 0. v16 aperta, runtime con `-c 0` health 200, controlli UI verificati con UI Automation read-only; composer non pilotato.
