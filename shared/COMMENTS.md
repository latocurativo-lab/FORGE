# COMMENTS — shared/ (Agente C)

Data: 2026-10-28

## Scelta JSON: implementazione minimal propria, NON vendor di cJSON

Il compito offriva due opzioni: vendor di cJSON (single-file MIT) in
`shared/vendor/` o implementazione minimal propria. Ho scelto la **seconda**,
e vendor `shared/vendor/` viene lasciato vuoto di proposito (questo file è il
segno; nessun agente deve riempirlo per sbaglio).

Motivazioni:

1. **Esigenza reale limitata.** FORGE usa JSON solo per: config del CLI,
   dump dell'audit log della state machine, e piccole risposte diagnostici.
   Serve: parse completo (+ escape `\uXXXX` incluse), builders, writer.
   Tutto questo è ~350 righe in `shared/src/forge_json.c`, già collaudato dai
   test unitari (`tests/unit/test_forge_json.c`).
2. **Superficie di manutenzione minore.** Vendorare cJSON signifieverebbe
   importare ~2.900 righe (cJSON.c + cJSON.h), una struct API con convenzioni
   sue, e la responsabilità di darcela via con l'upstream. Era la scelta
   giusta per un progetto grande; per un modulo usato in 3 chiamate è peso morto.
3. **Dipendenze esterne = 0.** Il mandato è "senza dipendenze esterne oltre
   libc"; anche una vendor directory è una dipendenza da tenere aggiornata
   (issue su JSON parser vendored sono una classe nota). Il parser che
   scriviamo è auditabile a colpo d'occhio e resistente ai DoS primitivi
   (malloc falliti trattati, input malformato restituisce NULL, non crash).
4. **Uniformità di stile.** L'intera codebase C di Agente C ora usa `fbuf`,
   `fstrdup` e la stessa convenzione di ownership ("chi costruisce libera");
   avere due modelli di memoria ( cJSON usa `next/prev` e `typedef` pubblici
   diversi) avrebbe creato attrito.

Anti-esito: se in futuro servisse JSON streaming, callback-style (SAX) o
performance parser >100 MB/s, si rivendoria cJSON in `shared/vendor/` come
meccanismo di rimpiazzo: l'API interna (`fjson_*`) è piccola e avvolgibile.

## Contenuto shared/src

| file            | ruolo |
|-----------------|-------|
| forge_util.[ch] | buffer dinamico `fbuf` + strdup portatili |
| forge_events.[ch]| eventi tipati genitore/runtime → harness + painter audit |
| forge_json.[ch] | JSON minimal: parse + builders + writer |
