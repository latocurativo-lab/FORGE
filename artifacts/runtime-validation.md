# Validazione runtime - 28 ottobre 2026

Esito parziale: il runtime esegue inferenza testuale e due controlli elementari passano; non sono verificati equivalenza numerica con un runtime/modello di riferimento, sicurezza del template, tool calling o contesti 16K/32K effettivamente popolati. Non usare questi risultati come certificazione del modello o per autorizzare strumenti agentici.

## Configurazione e riproduzione

RTX 4060 Ti 16 GB, driver 617.14, GPU condivisa con altri processi. Binario locale `runtime/upstream/llama.cpp/build/bin/Release/llama-server.exe`, fingerprint `b1-5220213`, modello locale `models/gemma-4-12b-it-Q4_K_M.gguf`. Il lock preesistente riporta SHA-256 `0a270ec9fe6b34f4a0d33992b6135117b484ebc4766ab76b51d4ae8c457e4c42`; non ricalcolato in questa prova.

Da root FORGE avviare il server, solo su loopback:

```powershell
runtime/upstream/llama.cpp/build/bin/Release/llama-server.exe -m models/gemma-4-12b-it-Q4_K_M.gguf -ngl 99 -fa on -c 4096 -t 4 -np 1 --host 127.0.0.1 --port 18932 --jinja --reasoning off
powershell -NoProfile -ExecutionPolicy Bypass -File runtime/bench/validate-api.ps1
```

Il secondo comando va eseguito in un altro terminale a server pronto. Il server non ha API key e segnala CORS aperto: loopback non equivale a un confine di sicurezza per browser o altri processi locali. Spegnerlo dopo le prove. Quello avviato per questa verifica è stato terminato.

## Risultati API

Richieste deterministiche `temperature=0`, `seed=42`, `cache_prompt=false`, `enable_thinking=false`; risposte integrali in `runtime/bench/validation-{arithmetic,reversal,length}.json`. Tempi sono misure singole, non benchmark statistici, e con GPU condivisa.

| Caso | Riferimento indipendente | Risposta | finish_reason | prompt / generazione token | prefill / decode token/s |
|---|---|---|---|---|---|
| 17 * 19 | 323 | 323 | stop | 28 / 4 | 75,0 / 30,6 |
| FORGE invertito | EGROF | EGROF | stop | 25 / 4 | 229,8 / 30,8 |
| Storia, limite 1 token | Troncamento atteso | The | length | 22 / 1 | 200,5 / non significativo |

I primi due confrontano il risultato con risposte esatte, non con un secondo motore di inferenza. La terza prova verifica che `length` non venga confuso con completamento naturale. I conteggi e i tempi provengono dalle risposte API. Baseline GPU globale prima dell'avvio 2191 MiB; lettura globale a server caricato 9782 MiB, non picco campionato né memoria attribuibile al processo. Non sono misure sostenute, non dimostrano contesto pieno né stabilità.

## Template, thinking e avviso

`/apply-template` per un messaggio user `Hello`, con thinking off, produce `<|turn>user\nHello<turn|>\n<|turn>model\n<|channel>thought\n<channel|>` (evidenza `runtime/bench/validation-template.json`). La [documentazione Google](https://ai.google.dev/gemma/docs/core/prompt-formatting-gemma4) descrive delimitatori di turno, attivazione via `<|think|>` e canale thought; la [scheda del distributore](https://huggingface.co/unsloth/gemma-4-12b-it-GGUF) descrive un blocco thought vuoto con thinking disabilitato e raccomanda `--jinja` e `enable_thinking=false`. La resa osservata è coerente per questo singolo esempio, non è confronto completo del template con l'originale né dimostrazione che il modello non emetta mai ragionamento. Le due risposte API non riportano `reasoning_content`; il parser può nascondere contenuto, quindi non costituiscono ispezione dei token grezzi.

L'[API Hugging Face](https://huggingface.co/api/models/unsloth/gemma-4-12b-it-GGUF/tree/main?recursive=true&expand=true) riporta per il file Q4_K_M messaggio Protect AI `This file is vulnerable to threat(s) PAIT-GGUF-100`, pur con campo status `safe`; JFrog lo classifica safe e VirusTotal 0/74. Il significato tecnico preciso e l'applicabilità del finding a questo runtime non sono stati verificati: scanner discordanti e hash coincidente non annullano l'avviso. Nessun test di exploit, audit completo del Jinja o validazione sandbox è stato svolto. Il log `runtime/bench/validation-server.err` riporta inoltre override dei tipi di alcuni token di controllo, da approfondire rispetto al tokenizer ufficiale.

## Blocchi e lavoro residuo

- Mancano confronto con logits/risposte di riferimento indipendente usando stessi pesi e tokenizer, e test significativi di qualità e tool calling.
- Mancano benchmark ripetuti con prompt effettivamente tokenizzati a 16K/32K, stop reason, monitoraggio picco e margine VRAM: i test brevi e i vecchi file `artifacts/bench/` non dimostrano tali capacità.
- Indagare PAIT-GGUF-100 e confrontare integralmente template/tokenizer con fonte ufficiale prima di esporre strumenti o dati sensibili.
