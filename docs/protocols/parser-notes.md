# parser-notes.md — decisioni di parsing Gemma 4 (Agente C)

Data: 2026-10-28. Implementazione: `agent/src/toolcall_parser.[ch]`.
Test: `agent/tests/test_toolcall_parser.c` (passano con MSVC 19.51 e gcc
WinLibs 15.x, vedi agent/BUILD.md).

## 1. Riduzione della grammatica

Il riferimento regex della documentazione ufficiale (Python) è:

```python
{'name': n, 'arguments': {...}} for n, args in
re.findall(r"<\|tool_call>call:(\w+)\{(.*?)\}<tool_call\|>", text, re.DOTALL)
```

Adottiamo ('\\c' = tokens):

```
output      := ( testo | tool_call | residui )*        ; tutto ciò che non è tool_call è testo raw
tool_call   := "<|tool_call>" "call:" NAME "{" args "}" "<tool_call|>"
arg         := KEY ":" ( QUOTED | BARE )
QUOTED      := "<|\"|>" VERBATIM "<|\"|>"              ; primo terminatore chiusa
BARE        := [^\,]+ fino a ',' a profondità 0        ; {},[],() seguiti a depth
```

Differenze rilevanti con la regex di riferimento (motivate):

- la regex con `.*?`+DOTALL matcha lo stesso testo ma ignora anomalie; il
  parser C è strutturale: anomalia => diagnóstico in `issues` / `problem`.
- NAME: regex = `\w+`; noi accettiamo [A-Za-z0-9_.] e rifiutiamo il resto
  (spazio incluso) con diagnóstico "malformed tool name". Non inventare
  chiamate con nomi corrotti.

## 2. Edge case e decisione

| # | caso                                      | decisione del parser  |
|---|-------------------------------------------|-----------------------|
| 1 | Più chiamate nello stesso output        | estratte tutte; parsing continuo dopo `<tool_call|>` |
| 2 | virgola dentro stringa quotata            | contenuto verbatim (`location:<|"|>Tokyo, JP<|"|>` → `Tokyo, JP`), no split |
| 3 | argomento nudo (number/bool/enum)         | until `,` a depth 0 di `{ [ (`; graffo/nested inside bare ammessi e depth-considerati |
| 4 | `call:nome{}` (argomenti vuoti)           | 0 args, call ben formata |
| 5 | manca `<tool_call|>` di chiusaura          | NESSUN record di call inventato; issue "unterminated tool call (missing <tool_call|>)"; il restante della call non (ri)legge: ci affidiamo a `result->raw` per forensics |
| 6 | manca `call:` dopo `<|tool_call>`         | issue, si resincronizza dal token stesso: un `get_weather{...}` malformato non diventa una call |
| 7 | `<|"|>` non chiusao                        | call estratta (raw span OK), `ok=0`, problem "unterminated quoted value (no closing <|\"|>)" |
| 8 | `<|"|>` in un valore (embedded delimiter) | PRIMA POLICY: il valore termina al PRIMO `<|"|>` successivo; il resto è "junk": si marca ok=0 con diagnosi esplicita, però il best-effort value resta (test_quote_token_inside_value). MAI invenzione di un terzo valore |
| 9 | output vuoto / niente tool                | 0 call, 0 issues (test_empty_and_plain) |
|10 | call nominate con spazio (`call:bad name`) | no record; diagnosi "malformed tool name". Rationale: nome errato NON deve eseguire nessun codice |
|11 | graffa } dentro argomento nudo            | depth-tracking: `regex:{from:<|"|>a+b<|"|>,to:...}` — le graffe dentro il BARE vanno contate, non chiudo al primo } |
|12 | troncamento a metà args (GenerationTruncated) | come 5: nessuna fake-call, issue+raw |
|13 | `<|tool_call>` dentro una quoted arg      | non è un problema: parse_args procede per span; il token interno non ri-entra nel fetch (il cursore si muove oltre alla closure) |
|14 | testo dopo `<tool_call|>` finale          | è output del modello (es. testo prolettico): non ci importa, il parser lo recepisce come testo, non lo tocca |

## 3. Policy del seriale rifiuta <|"|> dentro i valori

**Il formato non ha escape.** Quindi il serializer (`tc_response_add`,
`tc_quote_value`) rifiuta (ritorna -1) un valore che contiene `<|"|>`,
invece di produrre un blocco prevedibilmente corrotto. Cosa deve fare il
livello sopra: sanificare i valori dei tool (strip o sostituzione), MAI
inventare una sequenza di escape ad-hoc che il modello non ha mai visto in
training e che comunque andrebbe a corromperlo.

## 4. Cos'è "rispetta l'output grezzo"

Ogni `tc_result` possiede `raw`: la stringa INTERA che è entrata, viva per
log/audit/replay. Nella forensic dei troncamenti (M1, checkpoint/ripresa) è
il raw, non l'interpretazione, la fonte della verità.

## 5. Aperto / da validare con il modello reale (M1 gate)

- Il nome del tool può contenere `-`? La doc usa `\w+` (esempi snake_case).
  Alcuni dataset permettono tratteggio; il parser C NON attraversa `-` (lo
  rifiuta). Da rivedere su esempi reali di `ggml-org/gemma-4` prima di M2.
- JSON annidati generati dal modello dentro un argomento BARE: la grammatica
  ufficiale non li mostra; il nostro depth-tracking li supporta ma nessun
  test di verifica li chiama già-codificati. To verify on-device.
- `<|tool_response>` nello stesso output del modello: si assume che il
  modello NON lo generi mai da solo (è compito dell'host). Se lo vedessimo,
  è un dato: si registra issue, non lo eseguiamo.
