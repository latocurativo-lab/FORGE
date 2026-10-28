# FORGE - Audit ambiente (M0)

Generato da: scripts/setup/audit.ps1
Data: 2026-10-28
Macchina attesa (mandato): Windows 11 Pro, RTX 4060 Ti 16 GB, 32 GB RAM

---
===== FORGE AUDIT - ambiente di build =====
Data: 2026-10-28 22:12
------------------------------------------------------------
[OS]
OS: Microsoft Windows 11 Pro 64 bit
Build: 26300
------------------------------------------------------------
[GPU]
GPU: NVIDIA GeForce RTX 4060 Ti, 16380 MiB, 617.14
------------------------------------------------------------
[CUDA]
CUDA: Cuda compilation tools, release 13.3, V13.3.33
CUDA: Build cuda_13.3.r13.3/compiler.37862127_0
------------------------------------------------------------
[RAM e disco]
RAM totale: 31.1 GB
Disco C:: libero 154.8 GB / 931.5 GB
------------------------------------------------------------
[Strumenti di build]
gcc (WinLibs): OK - C:\Users\geekandhack\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe - gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r4) 16.1.0
clang: ASSENTE
cmake: OK - C:\Program Files\CMake\bin\cmake.exe - cmake version 4.4.2
python: OK - C:\Python314\python.exe - Python 3.14.5
node: OK - C:\Program Files\nodejs\node.exe - v26.3.0
git: OK - C:\Program Files\Git\cmd\git.exe - git version 2.54.0.windows.1
ninja: OK - C:\Users\geekandhack\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\ninja.exe - 1.13.2
------------------------------------------------------------
[MSVC / Visual Studio]
vswhere presente ma nessuna installazione VS

(cl.exe e disponibile solo nel Developer Command Prompt; non nel PATH di default.)

---

## Verdict di compatibilita

### Presente (verde)
- Windows 11 Pro 64 bit, build 26300: OK.
- GPU: NVIDIA GeForce RTX 4060 Ti, **16380 MiB VRAM** - come da mandato; driver NVIDIA 617.14.
- CUDA Toolkit **13.3** (nvcc V13.3.33, in PATH): la build CUDA del fork e fattibile in loco.
- gcc MinGW-W64 (WinLibs) 16.1.0 + ninja 1.13.2 (stessa toolchain winget): toolchain alternativa completa.
- cmake 4.4.2, git 2.54.0, python 3.14.5, node 26.3.0: tutti presenti.

### Mancante (rosso)
- **Visual Studio / MSVC completamente assente**: vswhere presente ma nessuna installazione VS,
  cl.exe non disponibile (nemmeno Build Tools). Nessun compiler MSVC sul sistema.
- clang: assente (non bloccante se si usa MSVC o MinGW).

### Cosa serve per la build CUDA di llama.cpp
Opzione consigliata (per Agente B, M0):
1. Installare **Visual Studio Build Tools 2022** con workload
   "Desktop development with C++" (MSVC v143 + Windows SDK). E la strada standard
   e piu testata per GGML CUDA su Windows, compatibile con CUDA 13.3.
2. Configura con:
   cmake -B build -G Ninja -DGGML_CUDA=ON -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
   (eseguendo da un Developer Command Prompt, cosi cl.exe e nel PATH).
3. Piano B (gia installato): MinGW (WinLibs gcc 16.1.0) + ninja + CUDA 13.3.
   GGML CUDA su MinGW e meno testata dai maintainer: tenere come fallback,
   preferire MSVC.

### Note
- RAM rilevata 31,1 GB (32 GB dichiarati, ~1 GB riservato hardware): conforme al mandato.
- Disco C: 155,3 GB liberi su 931,5: sufficienti per fork + build + modello Q4_K_M (~8 GB in models/).
- Python 3.14.5 presente; CONTEXTO dichiara "Python 3.12": discrepanza non bloccante
  per M0 (fork + build + audit); documentata qui.
- CUDA 13.3 e recentissimo: verificare matrice di supporto CUDA di llama.cpp nel
  commit pinnato del fork (compito Agente B); nota salvata in LICENSES/NOTICE e in
  docs/architecture/overview.md.
