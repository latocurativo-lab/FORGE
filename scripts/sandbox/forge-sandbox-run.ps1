<#
.SYNOPSIS
    FORGE sandbox runner — SKELETON. Default-deny: non esegue nulla senza flag espliciti.

.DESCRIPTION
    Strumento per eseguire codice/test generati SOLO dentro un confine approvato
    (opzioni: docker | wsl | winsandbox | hostuser). Non e' parte del normale EXEC
    dell'agente: va invocato manualmente dopo che l'utente ha approvato la policy
    (artifacts/forge-sandbox-design.md) e dopo che la suite negativa N1-N11 ha
    passato (artifacts/sandbox-runs/). Nessun di questi prerequisiti e' satisfatto
    al 2026-10-29: lo script DEVE fallire a scelta pura.

    ATTENZIONE: questo file e' uno scheletro di preparazione. La logica reale di
    docker run / wsl / .wsb / utente+Job Object va implementata dopo la firma
    dell'utente (S1-S5 del documento di design). NON eseguire prima.

.NOTES
    2026-10-29 — audit sub-agent. Sola creazione: nessuna esecuzione, nessun run.
#>
[CmdletBinding(SupportsShouldProcess = $true, DefaultParameterSetName = 'Deny')]
param(
    [ValidateSet('docker', 'wsl', 'winsandbox', 'hostuser')]
    [string] $Mode,

    [string] $WorkspacePath,          # directory con i file generati (sorgente di copia)
    [string] $Command,                # comando da eseguire dentro il confine
    [string] $ImageRef = '',          # solo docker: image@sha256:... (pinata per digest, mai :latest)
    [string] $WslDistro = '' ,        # solo wsl: distro usa-e-getta dedicata (mai kali-linux!)
    [int] $TimeoutSeconds = 300,
    [string] $MemoryLimit = '',
    [string] $CpusLimit = '',
    [int] $PidsLimit = -1,
    [int] $StdoutCapBytes = 1048576,

    # ---- Flag che DI DEFAULT RIFIUTANO se mancanti ----
    [switch] $AcceptSandboxNotValidated,   # il DEFAULT e' rifiutare: nessun prerequisito e' provato
    [switch] $IHaveReadForgeSandboxDesign, # conferma lettura di artifacts/forge-sandbox-design.md
    [switch] $AllowNetwork,                # default: rete OFF; attiverla richiede log e audit
    [switch] $ListTests                    # solo stampa della suite negativa, nessuna esecuzione
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$suite = @(
    'N1 cwd-exit', 'N2 write-outside', 'N3 network', 'N4 persistence',
    'N5 children-spawn', 'N6 timeout', 'N7 process-tree', 'N8 huge-output',
    'N9 crash', 'N10 symlink-junction', 'N11 cleanup'
)

if ($ListTests) {
    Write-Host 'Suite negativa di contenimento (da eseguire SOLO con approvazione utente):'
    $suite | ForEach-Object { Write-Host " - $_" }
    return
}

# --- Pre-flight: requisiti di firma e di policy ---
$designDoc = Join-Path $PSScriptRoot '..\..\artifacts\forge-sandbox-design.md'
if (-not (Test-Path -LiteralPath $designDoc)) { throw 'Design doc mancante: artifacts/forge-sandbox-design.md. Ripristinarlo prima di usare il runner.' }
if (-not ($IHaveReadForgeSandboxDesign -and $AcceptSandboxNotValidated)) {
    Write-Host 'REFUSED: prerequisiti di firma mancanti.' -ForegroundColor Yellow
    Write-Host 'Per eseguire c''e'' bisogno di:'
    Write-Host "  1) aver letto $designDoc"
    Write-Host '  2) -AcceptSandboxNotValidated (che dichiara che la suite negativa N1-N11 NON e'' ancora passata: nessun codice generato va eseguito)'
    Write-Host '  3) completare S1-S5 del design doc e superare la suite.'
    Write-Host 'Uso senza esecuzione: -ListTests | -WhatIf.'
    exit 2   # rifiuto pulito, nessun effetto
}

if ($PSCmdlet.ShouldProcess($Mode, 'Verifica prerequisiti engine (nessuna esecuzione)')) {
    switch ($Mode) {
        'docker' {
            if (-not $ImageRef -or $ImageRef -notmatch '@sha256:[0-9a-f]{64}') { throw 'ImageRef richiede digest sha256 esplicito (docker: immagine pinata, mai :latest).' }
            if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw 'docker client assente.' }
            $null = docker info 2>$null; if ($LASTEXITCODE -ne 0) { throw 'docker daemon non raggiungibile: verificare Docker Desktop.' }
            # TODO reale: docker run con --network none (o -$AllowNetwork), --memory, --cpus, --pids-limit,
            #      cap stdout, root fs read-only, copia workspace dentro (mai bind rw), timeout, ledger file.
        }
        'wsl' {
            if (-not $WslDistro) { throw 'WslDistro richiesta.' }
            if ($WslDistro -eq 'kali-linux') { throw 'kali-linux e'' l''ambiente dell''utente: vietato come sandbox.' }
            if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'wsl assente.' }
            # TODO reale: wsl --import distro usa-e-getta, copia workspace, rete off, limiti, timeout, cleanup.
        }
        'winsandbox' {
            if (-not (Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue)) { throw 'WindowsSandbox.exe non presente: feature opzionale da attivare con elevazione+reboot.' }
            # TODO reale: generare .wsb (niente drive host, no clipboard, no networking controllabile), Mappare workspace in copia.
        }
        'hostuser' {
            Write-Warning 'opzione hostuser NON e'' una sandbox: solo contenimento di riduzione del rischio (Job Object+utente+ACL+firewall).'
            # TODO reale: crea utente dedicato, Job Object, dir temp ACL, firewall outbound deny, allowlist exe.
        }
        default { throw 'Mode richiesta tra: docker|wsl|winsandbox|hostuser.' }
    }
}

Write-Error 'SKELETON: nessuna esecuzione implementata. Lo script esce senza aver avviato processi o copie.'
exit 3
