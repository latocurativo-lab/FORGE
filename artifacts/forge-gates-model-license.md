# Gate modello: PAIT-GGUF-100 + licenza — audit e prove

**Data:** 2026-10-29 · **Autore:** audit sub-agent (sola lettura + prove di metadati) · **Repo:** FORGE @ `C:\Users\geekandhack\Desktop\FORGE`

**Esito sintetico (onesto):**
- `PAIT-GGUF-100` → **risolto come classificazione** (minaccia di classe GGUF-SSTI, non indicatore di alterazione del file), con **applicabilità al runtime FORGE analizzata ma non certificata** e **scansione del template locale passata**. La firma finale resta una **decisione dell'utente**.
- Licenza → **l'ipotesi precedente "Gemma Terms of Use" non è supportata dalle fonti primarie attuali**: Gemma 4 è pubblicato da Google con **licenza Apache-2.0**, coerentemente dichiarato anche dai distributori (unsloth, ggml-org) e dalla pagina licenza ufficiale. La decisione di aggiornare `LICENSES/NOTICE.md` e la policy di redistribuzione resta all'**utente**; questo file non modifica NOTICE.md.
- Tokenizer → **audit parziale**: template confrontato byte-per-byte; l'elenco token e gli override segnalati dal runtime restano **non verificati** (vedi §6).

---

## 1. Cosa dice esattamente il finding

L'API Hugging Face (`https://huggingface.co/api/models/unsloth/gemma-4-12b-it-GGUF/tree/main?recursive=true&expand=true`, consultata oggi 2026-10-29) riporta per **ogni file di modello** del repo (BF16, IQ4_NL, IQ4_XS, Q3_K_M/S, Q4_0, Q4_1, **Q4_K_M**, Q4_K_S, Q5_K_M/S, Q6_K, Q8_0, UD-*) e per il file locale usato da FORGE:

```json
"securityFileStatus": {
  "status": "safe",
  "protectAiScan": {
    "status": "safe",
    "message": "This file is vulnerable to threat(s) PAIT-GGUF-100.",
    "reportLink": "https://insights-db.paloaltonetworks.com/models/unsloth/gemma-4-12b-it-GGUF/fc034cfff751157913579611efad8462ac1be606/files?blob-id=8e8764be282692a1d62bc76c6486021857385b9e"
  },
  "jFrogScan": { "status": "safe", "message": "Safe GGUF model",
    "reportLink": "https://research.jfrog.com/model-threats/gguf-ssti" },
  "virusTotalScan": { "status": "safe", "message": "0/74 engines detect it as malicious." }
}
```

Osservazioni chiave (nuove, verificabili):

1. **Il flag copre TUTTI i GGUF di modello del repo** (incluso Q4_K_M `blob-id 8e8764be…`), mentre i file senza chat-template (`.gitattributes`, `README.md`, `imatrix_unsloth.gguf_file`, `mmproj-*`) ricevono "no security findings". Questo è coerente con un **finding di classe legato ai GGUF che contengono `tokenizer.chat_template`**, non con un indicatore di manomissione del singolo file.
2. Il link JFrog agganciato dallo scanner punta a **GGUF-SSTI** ("GGUF model attempting template injection for arbitrary code execution"). La pagina JFrog (fetched, testo integrale in cache) spiega: un GGUF può contenere un template Jinja2 in `tokenizer.chat_template`; se caricato in un **motore Jinja2 Python non sandboxato** porta a SSTI con esecuzione di codice. L'unico caso noto pubblicamente: **CVE-2024-34359** ("Llama Drama"), sfruttabile **solo** con `llama-cpp-python < 0.2.72`; non è documentato come sfruttabile in llama.cpp (C++) o ollama.
3. Report primario Protect AI / Palo Alto "Insights DB": la pagina (`reportLink`) risponde come app JS; ho potuto confermare la dicitura del messaggio e "Last scanned: Jul 24, 2026" ma **non ho potuto scaricare il dettaglio per-file come testo** — il rapporto primario scaricabile resta un passi già risolti mancante (vedi §7).

## 2. Integrità del file locale (nessun re-hashing di 7 GB)

Valori **ripresi dai registri già esistenti** (non ricalcolati):

| Campo | Valore | Fonte |
|---|---|---|
| Path locale | `models/gemma-4-12b-it-Q4_K_M.gguf` (esistente, 7.121.861.440 byte, LastWrite 2026-10-28 22:23:29) | `Get-Item` (oggi) |
| SHA-256 | `0a270ec9fe6b34f4a0d33992b6135117b484ebc4766ab76b51d4ae8c457e4c42` | `models/MODEL.lock` riga 13 + default `ExpectedSha256` in `scripts/launch/forge-local.ps1` riga 22 |
| Size | 7.121.861.440 byte, controllo biforcato nel launcher (`ExpectedBytes`) | `MODEL.lock` + launcher riga 23/130 |
| Corrispondenza upstream | LFS oid del file `gemma-4-12b-it-Q4_K_M.gguf` nel repo unsloth = **stesso SHA-256** 0a270ec9… , size identica | API HF tree (oggi) |

Conclusione integrità: la catena hash locale ↔ pubblica è **coerente** da fonti già registrate; il file non è stato alterato dopo la registrazione. L'hash prova integrità rispetto alla fonte, **non** sicurezza dei pesi.

## 3. Template: confronto locale ↔ repo (nuova prova, lets verbatim)

**Metodo (read-only, solo metadati GGUF):** script `artifacts/model-license-evidence/gguf-kv-audit.ps1` (copia della run effettuata da `Temp\opencode`) parsa l'header GGUF (magic, versione 3, 667 tensori, 58 KV) e legge solo i metadati — mai il file completo in streaming.

**Esito della run (2026-10-29):**

- `general.architecture = gemma4`, `general.name = Gemma-4-12B-It`, `general.file_type = 15` (Q4_K_M), `tokenizer.ggml.bos_token_id = 2`, `tokenizer.ggml.eos_token_id = 106`.
- `tokenizer.chat_template` estratto: **18.922 caratteri / 18.924 byte UTF-8**, SHA-256 `845f1ee48e39fc942fe190da9df6a1c5db229e17a96ea08966ad1c9274e73d1b`. Copia registrata: `artifacts/model-license-evidence/local-unsloth-chat-template.jinja`.
- **Confronto byte-per-byte** con `gguf.chat_template` pubblicato dall'API del repo unsloth (repo head `fc034cfff751157913579611efad8462ac1be606`, commit "Added Gemma official chat template update", 2026-07-17): `EQUAL_LOCAL = True` (stesso hash `845f1ee4…`, stessa lunghezza 18.924). Il file che gira in locale porta il template dichiarato dal distributore oggi.
- **Scanner SSTI sul template estratto** (marker `__class__`, `__subclasses__`, `__builtins__`, `subprocess`, `popen`, `os.system`, `eval(`, `exec(`, `__import__`, payload Jinja tipici): **NESSUN marker trovato**. Il template è un formatter DSL per tool-call, non contiene payload di esecuzione.
- **Confronto con ggml-org/gemma-4-12B-it-GGUF** (canale "ufficiale" gemma, template intitolato "Google Gemma 4 Canonical Chat Template", 18.683 byte, SHA-256 `ae53464bf3be25802b3a5b37def7fd89667067d7577049b3b2d74c4d8de4c6d4`): **DIVERSO** dal template unsloth. Diff registrata in `artifacts/model-license-evidence/template-diff-fc.txt`; differenze sostanziali: (a) header commentary ggml-org, (b) gestione `tool_calls[].function.arguments` stringa — unsloth rende non-fatalamente (strippa `{…}`), ggml-org `raise_exception`. Stessa baracca DSL (`<|turn>`, `<turn|>`, `<|channel>thought`, `<|tool_call>call:` …).
  - Ne segue una correzione da fare a `models/MODEL.lock`: la riga 19 afferma "chat template **Google Gemma 4 canonical**", ma i due canali hanno template **diversi**; quello in uso è quello unsloth (adesso provato byte-identico). Anche `repo_sha` riga 10 (`202658970530…`) è oggi lo sha del repo **ggml-org**, non di unsloth (head `fc034cff…`) — il commento "unsloth @ 2026-05-29 + update" è fuorviante. Da correggere in una passata successiva (non modificato in questo audit).
- La resa osservata in `artifacts/runtime-validation.md` (`/apply-template` → `<|turn>user … <|channel>thought\n<channel|>`) resta coerente con i delimitatori del template estratto; il confronto è su un probe singolo (già registrato).

**Applicabilità al runtime FORGE (analisi, non certificazione):**
`llama-server` è il fork C++ locale `runtime/upstream/llama.cpp` (commit `5220213`), avviato con `--jinja` dal launcher. llama.cpp implementa un sottoinsieme Jinja in C++ (nessun motore Python Jinja2), quindi il vettore documentato del finding (SSTI Python via `llama-cpp-python <0.2.72`) **non si applica per come è documentato**. Non ho però eseguito un audit del C++ Jinja di llama.cpp né verificato la versione patchata del fork: dichiarato **"analisi non certificata"**, non "sicuro".

## 4. Licenza del modello

**Fonti consultate oggi (2026-10-29), con estratti:**

| Fonte | Contenuto rilevante |
|---|---|
| API HF `google/gemma-4-12B-it` | `gated: False`, `cardData.license = apache-2.0`, `license_link = https://ai.google.dev/gemma/docs/gemma_4_license` |
| README raw `google/gemma-4-12B-it` | YAML `license: apache-2.0` + "**License**: Apache 2.0" (link alla stessa pagina) |
| Pagina `ai.google.dev/gemma/docs/gemma_4_license` | Intitolata **"Gemma 4 license"**; rende il testo integrale della **Apache License 2.0**; "Last updated 2026-04-01 UTC"; voce di navigazione "Gemma 4 license → /gemma/apache_2" |
| API HF `ggml-org/gemma-4-12B-it-GGUF` | `cardData.license = apache-2.0`, `gated: False` |
| API HF `unsloth/gemma-4-12b-it-GGUF` + README raw | tag `license: apache-2.0`, `license_link` → stessa pagina Gemma 4; card "**License**: Apache 2.0" |
| LICENSES/NOTICE.md §1 (locale) | Afferma: "distribuito con la licenza proprietaria di Google (Gemma Terms of Use), NON Apache 2.0 puro", con checkbox di Fase 1 aperte |

**Conclusione licenza:** per la famiglia **Gemma 4** le fonti primarie concordano su **Apache-2.0**; il fondo del conflitto è che `LICENSES/NOTICE.md` (M0, 2026-10-28) ha fissato un'ipotesi basata sui Gemma precedenti ("Gemma Terms of Use") che oggi **non è confermata dalle fonti del modello in uso**. `models/MODEL.lock` ("apache-2.0 (repo unsloth; base model google/gemma-4-12B-it)") risulta invece **coerente** con le fonti odierne.

Restano condizionamenti documentati (da citare in NOTICE aggiornato):
- **Gemma "Prohibited use policy"** (linkato dalla nav di ai.google.dev/gemma): applicabile ai modelli Gemma indipendentemente dalla licenza del codice/pesi — contenuto **non auditato** in questa passata.
- Redistribuzione dei pesi Q4_K_M derivati: con Apache-2.0 è permessa con copy of license/attribution; la regola di progetto "no ridistribuzione del bundle + modello, download utente finale" (NOTICE.md §1) resta **una scelta di policy**, non più un obbligo di licenza.

## 5. Verdetto sui gate

| Gate | Stato | Motivo |
|---|---|---|
| `PAIT-GGUF-100` (classe finding) | **DECISO=PENDENTE** (firma utente) | Classificazione documentata: finding di classe GGUF-SSTI, flag su tutti i file-modello del distributore, template locale byte-identico al distributore, marker SSTI assenti, vettore documentato non applicabile al runtime C++. Mancano: rapporto primario scaricabile in archivio, audit Jinja C++, **firma/accettazione dell'utente**. |
| Licenza modello | **DECISO=PENDENTE** (decisione utente) | Prove raccolte che Gemma 4 = Apache-2.0; contraddizione NOTICE.md documentata; aggiornamento NOTICE + policy redistribuzione = decisioni utente (questo audit non scrive NOTICE.md). |
| Tokenizer/template completo | **PARZIALE / NON VERIFICATO** | Template: confrontato. Tokenizer completo (tabelle token/tipi) e override segnati in `runtime/bench/validation-server.err` (token 1 `<eos>`, 50 `<|tool_response>`, 212 `</s>` "control-looking … overridden"): **non confrontati** con la fonte ufficiale. |

## 6. Elementi "non verificato" (dichiarati esplicitamente)

1. Rapporto primario Palo/Protect AI per-file: pagina JS, **non** scaricato in archivio. Non è dimostrato quale regola scanner esatti il flag (dedotto dal cross-link JFrog + copertura di tutti i GGUF con chat_template).
2. `llama-cpp-python` non è nel progetto (FORGE usa llama.cpp C++), ma **nessuna prova** che il fork locale non sia esposto a varianti del template-engine: audit Jinja C++ **non eseguito**.
3. Tokenizer: tabelle non estratte/confrontate. Gli override dei control-token restano un blocco indicato in `artifacts/m0-status.md`.
4. Contenuto integrale della "Gemma Prohibited Use Policy": **non letto**.
5. Codice runtime/patch del fork (`runtime/patches/`): non verificata l'equivalenza con upstream `5220213` in questa passata.

## 7. Decisioni restanti all'utente (checklist firma)

- [ ] **D1 — Accettazione finding:** accettare `PAIT-GGUF-100` come finding di classe con applicabilità analizzata (rimane il passo dell'archivio del rapporto: scaricare il report Insights DB come HTML/PDF con il browser e salvarlo in `artifacts/model-license-evidence/`).
- [ ] **D2 — NOTICE:** aggiornare `LICENSES/NOTICE.md` §1 da "Gemma Terms of Use" ad **"Apache-2.0 (Gemma 4)"** con le fonti di §4 + citare Gemma Prohibited Use Policy; correggere le due note fuorvianti in `models/MODEL.lock` (template canonical → unsloth; repo_sha → ggml-org).
- [ ] **D3 — Redistribuzione:** confermare o modificare la policy "no redistribuzione del bundle col modello"; con Apache-2.0 la redistribuzione è ammessa con attribution, resta scelta di prodotto.
- [ ] **D4 — Proseguimento:** autorizzare l'uso del modello per un run TaskFlow agentico **solo dopo** il gate sandbox (artefatto `forge-sandbox-design.md` + `scripts/sandbox/forge-sandbox-run.ps1` superati nella suite negativa).
- [ ] **D5 — Tokenizer (opzionale ma consigliato):** completare il confronto token/tipi con ggml-org o con il tokenizer ufficiale Gemma 4 e registrare gli override in un report dedicato.

## 8. Prove archiviate

- `artifacts/model-license-evidence/local-unsloth-chat-template.jinja` — template estratto dal GGUF locale (18.924 byte, SHA-256 `845f1ee4…d1b`).
- `artifacts/model-license-evidence/ggml-org-chat-template.jinja` — template del canale ufficiale ggml-org (18.683 byte, SHA-256 `ae53464b…6d4`).
- `artifacts/model-license-evidence/template-diff-fc.txt` — diff dei due template.
- `artifacts/model-license-evidence/gguf-kv-audit.ps1` — script di estrazione metadati (read-only; eseguito una volta dal temp; esecuzione futura a scelta dell'utente).
