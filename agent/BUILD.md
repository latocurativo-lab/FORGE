# BUILD — Agente C (shared/ + agent/ + tests/)

Data: 2026-10-28.

## Compilatori audit sul sistema (mandato: `where gcc`, `where clang`, `where cl`)

| tool               | esito | dove                                                              |
|--------------------|-------|-------------------------------------------------------------------|
| **gcc**            | TROVATO | WinLibs UCRT (mingw64) via Winget: `...\WinLibs.POSIX.UCRT...\mingw64\bin\gcc.exe` (gcc 15.x) |
| mingw32-make       | TROVATO | stesso pacchetto mingw64                                         |
| cmake              | TROVATO | `C:\Program Files\CMake\bin\cmake.exe` + copy nel mingw64         |
| clang              | ASSENTE | —                                                                 |
| cl (MSVC)          | ASSENTE da `where` MA presente: Visual Studio 2026 Build Tools (MSVC 19.51, Windows SDK 10.0.26100), visto dal generatore CMake |

Conclusione: build e test **eseguiti per davvero con DUE toolchain**:
1. CMake (default generator → Visual Studio 2026 / MSVC 19.51), e
2. gcc WinLibs UCRT a riga di comando (esplicito, vedi sotto).

## 1. Build via CMake (consigliata)

Dalla radice del progetto `C:\Users\geekandhack\Desktop\FORGE`:

```bat
cmake -S agent -B build-agent -DCMAKE_BUILD_TYPE=Release
cmake --build build-agent --config Release
ctest --test-dir build-agent -C Release --output-on-failure
```

Binari di test: `build-agent/Release/test_toolcall_parser.exe`,
`test_state_machine.exe`, `test_forge_events.exe`, `test_forge_json.exe`.

Nota: se si vuole forzare gcc invece di MSVC:
```bat
cmake -S agent -B build-agent-gcc -G "MinGW Makefiles" ^
  -DCMAKE_MAKE_PROGRAM=mingw32-make
```
(serve `mingw64\bin` nel PATH per mingw32-make + gcc)

## 2. Build diretta con gcc (nessun CMake)

```bat
set PATH=C:\Users\geekandhack\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%

gcc -std=c11 -Wall -Wextra -Ishared\src -Iagent\src ^
  agent\src\toolcall_parser.c agent\src\state_machine.c ^
  shared\src\forge_util.c shared\src\forge_events.c shared\src\forge_json.c ^
  agent\tests\test_toolcall_parser.c -o test_toolcall_parser.exe

gcc -std=c11 -Wall -Wextra -Ishared\src -Iagent\src ^
  agent\src\toolcall_parser.c agent\src\state_machine.c ^
  shared\src\forge_util.c shared\src\forge_events.c shared\src\forge_json.c ^
  agent\tests\test_state_machine.c -o test_state_machine.exe

gcc -std=c11 -Wall -Wextra -Ishared\src ^
  shared\src\forge_util.c shared\src\forge_events.c shared\src\forge_json.c ^
  tests\unit\test_forge_events.c -o test_forge_events.exe

gcc -std=c11 -Wall -Wextra -Ishared\src ^
  shared\src\forge_util.c shared\src\forge_events.c shared\src\forge_json.c ^
  tests\unit\test_forge_json.c -o test_forge_json.exe
```

## 3. Risultati dell'ultima esecuzione (2026-10-28)

- MSVC 19.51 (CMake/Release): **4/4 test suite passano** (`ctest`: 100%).
- gcc 15.x WinLibs (`-Wall -Wextra`): **4/4 suite passano, zero warning**.

```
1/4 test_toolcall_parser ........  Passed   (13 casi parsing + serializer)
2/4 test_state_machine ........  Passed   (6 gruppi: happy path, illegali, recovery, cancel, fail, dump JSON)
3/4 test_forge_events ........  Passed   (8 tipi evento + null-safety)
4/4 test_forge_json ..........  Passed   (parse, escape, \uXXXX+surrogati, builders/writer, round-trip)
```

## 4. Cosa NON è compilabile (gap documentati)

- Nulla è dipendente dal modello: tutta la build gira senza llama.cpp/Gemma 4
  (per design: fondazione del protocollo testabile davvero in unit-test).
- `clang`: assente dalla macchina; il codice non fa nulla di non-standard
  (C11 + libc), quindi dovrebbe compilarci così com'è — da verificare dopo
  l'eventuale install.
- Nessuna dipendenza esterna (CUDA, llama.cpp ecc.) in questo perimetro:
  il codice è C11 solo libc.
