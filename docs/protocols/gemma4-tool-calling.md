# Protocollo tool-calling Gemma 4 — Specifica ufficiale FORGE

Stato: **normativo per il runtime FORGE (M1) e per l'harness (Agente C).**

Fonte verificata il 2026-10-28:
https://ai.google.dev/gemma/docs/capabilities/text/function-calling-gemma4
(data di aggiornamento dichiarata dalla pagina: 2026-06-04 UTC). La pagina
richiama il portale gemma.ai come destinazione futura: questa specifica resta
il riferimento stabile del progetto; tutti gli esempi qui sotto sono copiati
testualmente dalla pagina ufficiale.

Nota: la pagina mostra l'uso via Hugging Face `transformers>=5.10.1`,
ma **il formato è proprietà del modello, non della libreria**: nel fork
llama.cpp (M1) riproduciamo gli stessi token speciali, e parser/serializer di
`agent/src/toolcall_parser.[ch]` parlano esattamente questo formato.

---

## 1. Token e struttura

| token                                  | ruolo                                                  |
|----------------------------------------|--------------------------------------------------------|
| `<bos>`                                | inizio stream di dialogo                               |
| `<|turn>system` / `<|turn>user` / `<|turn>model` | apertura di un turno di quel ruolo           |
| `<turn|>`                              | chiusura di un turno                                   |
| `<|tool>declaration:NAME{...}<tool|>`  | dichiarazione di tool (nel turno `system`)             |
| `<|tool_call>call:NAME{ARGS}<tool_call|>` | chiamata a tool emessa dal modello                  |
| `<|tool_response>`                     | apertura della risposta agli strumenti                 |
| `response:NAME{...}`                   | payload della risposta, dentro `<|tool_response>`      |
| `<tool_response|>`                     | chiusura della risposta                                |
| `<|"|>` `...` `<|"|>`                  | delimitatori di stringa (stesso token a apri/chiudi)   |

Esempio reale (storia completa, copiata dalla documentazione):

```
<bos><|turn>system
You are a helpful assistant.<|tool>declaration:get_current_weather{description:<|"|>Gets the current weather in a given location.<|"|>,parameters:{properties:{location:{description:<|"|>The city and state, e.g. "San Francisco, CA" or "Tokyo, JP"<|"|>,type:<|"|>STRING<|"|>},unit:{description:<|"|>The unit to return the temperature in.<|"|>,enum:[<|"|>celsius<|"|>,<|"|>fahrenheit<|"|>],type:<|"|>STRING<|"|>} },required:[<|"|>location<|"|>],type:<|"|>OBJECT<|"|>} }<tool|><turn|>
<|turn>user
Hey, what's the weather in Tokyo right now?<turn|>
<|turn>model
<|tool_call>call:get_current_weather{location:<|"|>Tokyo, JP<|"|>}<tool_call|><|tool_response>response:get_current_weather{temperature:15,weather:<|"|>sunny<|"|>}<tool_response|>The current weather in Tokyo is 15 degrees Celsius and sunny.<turn|>
```

Osservazioni strutturali:

1. Le dichiarazioni stanno **dentro il turno `system`**, dopo il testo di
   sistema, chiuse da `<tool|>` e poi da `<turn|>`.
2. Nella fase "Model's Turn" (output fresco del modello) la generazione si
   **ferma sul token `<|tool_response>`**; il payload `response:...` e la
   chiusura `<tool_response|>` sono scritti dall'host (fase "Developer's
   Turn" del documento).
3. Nella storia completa la risposta viene ripresentata nel **turno del
   modello** in questo ordine: `<|tool_call>...</tool_call|>` poi
   `<|tool_response>response:...<tool_response|>` poi il testo finale
   naturale e infine `<turn|>`.

## 2. Quoting stringhe

- Ogni valore stringa è racchiuso in `<|"|>` ... `<|"|>` (lo stesso token a
  aprire e chiudere).
- Numeri, booleani ed enum viaggiano **nudi**:
  `temperature:15`, `on:true`, `unit:<|"|>celsius<|"|>`.
- Virgole e graffe dentro un valore quotato NON sono separatori. Esempio
  reale: `location:<|"|>Tokyo, JP<|"|>` — la virgola resta contenuto.
- Le virgole **fuori** dal quoting separano argomenti o elementi d'array:
  `enum:[<|"|>celsius<|"|>,<|"|>fahrenheit<|"|>]`.
- **Il formato non prevede escape**: non esiste una sequenza per
  rappresentare `<|"|>` dentro un valore. Regola operativa FORGE (dettagli in
  parser-notes.md): chi produce valori deve sanificare le sequenze dei token
  del protocollo nel testo; il serializer di FORGE rifiuta (`-1`) un valore
  che contiene già `<|"|>` invece di corrompere la conversazione in silenzio.
- Il quoting si applica anche nella dichiarazione (descrizioni, tipi, nomi
  dei required) come si vede nell'esempio di §1.

## 3. Ciclo completo (le tre fasi del documento ufficiale)

```
[1] Modello:
    <|tool_call>call:get_current_weather{location:<|"|>Tokyo, JP<|"|>}<tool_call|><|tool_response>

[2] Host (Developer's Turn):
    - parse (agent/src/toolcall_parser.c) -> nome + argomenti
    - esecuzione protetta del tool
    - appese alla storia, ruolo assistant:
        "tool_calls":    [{"function": {"name": "get_current_weather",
                                        "arguments": {"location": "Tokyo, JP"}}}]
        "tool_responses": [{"name": "get_current_weather",
                            "response": {"temperature": 15, "weather": "sunny"}}]

[3] Host re-marshalling nell formato:
    <|tool_response>response:get_current_weather{temperature:15,weather:<|"|>sunny<|"|>}<tool_response|>

[4] Modello:
    "The current weather in Tokyo is 15 degrees Celsius and sunny." + <turn|>
```

La documentazione ufficiale cita anche la forma "python-friendly": l'host
appende alla conversazione un messaggio con `role=assistant` che porta
insieme `tool_calls` e `tool_responses`; il template di chat ricava da lì i
token. Per il fork llama.cpp FORGE questa forma logica va tradotta
direttamente nei token di §1: la canonica è quella.

### Multiple call per turno

Per più chiamate indipendenti nello stesso turno, la documentazione mostra
che ogni risposta è un elemento separato:

```
"tool_responses": [
  { "name": "function_name_1", "response": {...} },
  { "name": "function_name_2", "response": {...} }
]
```

Il modello può emettere più blocchi `<|tool_call>...</tool_call|>` nel medesimo
output (il parser di FORGE li estrae tutti, vedi `test_multiple_calls`).
L'host risponde con altrettanti `response:NAME{...}` nello stesso blocco
`<|tool_response> ... <tool_response|>`.

## 4. Thinking mode

Con `enable_thinking=True` il modello produce **reasoned text PRIMA della
call**. Esempio reale della documentazione: la prompt "Hey, I'm in Seoul.
Is it good for running now?" genera prima il ragionamento (analisi della
richiesta, scelta del tool `get_current_weather`, decisione dei parametri)
e poi `call:get_current_weather{location:<|"|>Seoul<|"|>}`; l'API
`processor.parse_response` separa `thinking` / `content` / `tool_calls`.

Regole normative per FORGE:

1. Il reasoning è testo normale nel turno del modello, prima di `<|tool_call>`:
   l'harness NON lo inoltra all'utente come risposta, e per l'audit log è
   un flusso separato da quello del tool-call.
2. Decisione già chiusa in CONTEXTO.md: thinking mode **OFF di default**,
   `enable_thinking` a flag esplicito.
3. **Caveat del mandato** (nota della issue OpenWebUI nota): dopo l'uso di
   un tool web il reasoning può degradare. Implicazioni operative:
   - non fidarsi del reasoning generato DOPO una `tool_response` web:
     nome e argomenti della chiamata successiva vanno validati uno per uno
     prima dell'esecuzione (la doc stessa detta: *"Always validate function
     names and arguments before execution"* — è regola dell'harness, non una
     raccomandazione facoltativa);
   - tetto di token al reasoning nei turni successivi a tool web
     (Mitigazione: limite stop con `FE_STOP_TRUNCATED`, la state machine
     decide poi RECOVERING, non loop);
   - la degradazione non deve mai mandare in crash il parser: tutto output
      malformato finisce in diagnostics, mai pilotato "a mano".

## 5. Mappatura stop-reason (obiettivo M1: disambiguare)

Le quattro condizioni che si possono confondere se si guarda solo al token
terminale:

| condizione osservata                                    | stop reason FORGE          | evento                              |
|---------------------------------------------------------|----------------------------|-------------------------------------|
| `<tool_call|>` e poi stop sul `<|tool_response>`        | `FE_STOP_TOOL_REQUEST`     | `FE_TOOL_CALL_DETECTED` + `FE_ASSISTANT_TURN_COMPLETED(stop=tool_request)` |
| `<turn|>` con solo testo (nessuna call pendente)        | `FE_STOP_TURN_END`         | `FE_ASSISTANT_TURN_COMPLETED` (fine del turno del modello: dialogo può continuare) |
| eos (`<end_of_turn>`/`<eos>`) senza `<turn|>`            | `FE_STOP_MESSAGE_END`      | `FE_ASSISTANT_TURN_COMPLETED` (fine messaggio: messaggio completo ma non è una fine di turno ben formata) |
| output interrotto a metà call (max_tokens / length)      | `FE_STOP_TRUNCATED`        | `FE_GENERATION_TRUNCATED`           |
| contesto esaurito (limite token)                        | — (runtime)                | `FE_CONTEXT_LIMIT_REACHED`          |
| interruzione dal volere utente/sistema                   | `FE_STOP_CANCELLED`        | `FE_GENERATION_CANCELLED`           |
| errore runtime (OOM CUDA, ecc.)                          | — (runtime)                | `FE_RUNTIME_ERROR`                  |

Regole di disambiguazione del runtime:

1. `fine turno != fine messaggio`: `<turn|>` è la fine NORMALE del turno del
   modello; un eos senza `<turn|>` è un messaggio completo ma con formato
   fuori standard (mai trattato come fine di dialogo pulita: si registra e
   si normalizza nel prossimo prompt).
2. `richiesta tool`: la presenza di `<|tool_call>` con chiusura ben formata
   prevale su tutto il resto: anche se dopo ci fosse testo, l'harness esegue
   i tool e smista il testo nella state machine (VERIFYING/RECOVERING),
   senza euristico.
3. `troncamento`: call aperta SENZA `<tool_call|>` di chiusura, output
   interrotto da budget di token o da `<|tool_response>` mancante = troncato,
   MAI "call riuscita": il parser lo marca con issue, la state machine va in
   RECOVERING, e il checkpoint conserva il raw per eventuale ripresa.

## 6. Caveat Automatic vs Manual Schemas (dalla doc)

La conversione automatica da firma Python a JSON schema **perde dettagli** su
parametri oggetto complessi (esempio della doc: `update_config(config: Config)`
diventa "A Config object" senza le proprietà interne). Per FORGE:

- gli strumenti integrati dichiarano lo schema a mano (più fedele);
- tool type-checked: `properties` annidati espliciti, `required` esplicito;
- l'host valida argomenti contro la dichiarazione prima di eseguire.

## 7. Riferimenti interni

- Implementazione parser: `agent/src/toolcall_parser.h/.c`
- Test: `agent/tests/test_toolcall_parser.c` (casi: singola call, multiple,
  argomenti comma dentro quote, malformata, `<|"|>` nel valore, vuoto,
  troncamento a metà call)
- Decisioni di parsing: `docs/protocols/parser-notes.md`
- State machine agentica: `agent/src/state_machine.h/.c`
