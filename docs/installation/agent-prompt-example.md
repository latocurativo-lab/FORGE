# Esempio di prompt per un agent su FORGE

Endpoint locale: `http://127.0.0.1:18932/v1` (OpenAI-compatible, solo loopback).
Da Docker: `http://host.docker.internal:18932/v1`. Health: `GET /health`.

## Prompt da dare a un agent/IDE configurato su FORGE

Questo è il prompt minimo che fornisce a un agent le informazioni di contesto
che il server non comunica da solo (modello attivo, limiti, comportamento di
serving):

```text
Il backend LLM è un server locale FORGE su http://127.0.0.1:18932/v1
(OpenAI-compatible; model name: forge/qwen36-uncensored oppure forge/qwen36-local).

Proprietà da rispettare:
- Il server serve UN modello per volta (non richiedere switching a caldo;
  la variante si cambia rilanciando il launcher sul PC host).
- Il contesto utile è 65536 token: per task lunghi riassume e punta a rilievi
  puntuali invece di appendere file interi.
- Le completion supportano tool calling strutturato OpenAI (tools +
  tool_calls) e thinking in formato DeepSeek; non inviare marker custom.
- Se la latenza precipita a cifre a singola cifra di tok/s, segnala: è quasi
  sempre un problema di config del server, non tuo.
```

## Config rapida per harness comuni

OpenCode (`opencode.json`):

```json
{
  "provider": {
    "forge": {
      "npm": "@ai-sdk/openai-compatible",
      "name": "FORGE local",
      "options": { "baseURL": "http://127.0.0.1:18932/v1" },
      "models": {
        "qwen36-uncensored": { "name": "Qwen3.6 35B A3B (Abliterated)" },
        "qwen36-local":      { "name": "Qwen3.6 35B A3B (Base)" }
      }
    }
  }
}
```

Qualsiasi client OpenAI-compatibile:

```
base_url: http://127.0.0.1:18932/v1
api_key:  qualsiasi valore non vuoto (il server non autentica: è loopback)
model:    qwen36-uncensored | qwen36-local
```

## Verifica rapida da PowerShell

```powershell
(Invoke-RestMethod http://127.0.0.1:18932/health).status   # atteso: ok
curl.exe http://127.0.0.1:18932/v1/models                  # elenco modelli esposti
```
