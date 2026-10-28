# FORGE - audit ambiente M0
# Rileva: OS, GPU (nvidia-smi), CUDA, RAM, spazio disco C:, compiler,
# cmake, python, node, git, ninja, Visual Studio. Output testuale pulito.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup\audit.ps1

$ErrorActionPreference = 'SilentlyContinue'

function Get-CmdOut([string]$exe, [string[]]$exeArgs) {
    $c = Get-Command $exe -ErrorAction SilentlyContinue
    if (-not $c) { return $null }
    $out = & $exe @exeArgs 2>&1 | ForEach-Object { "$_" }
    return ($out -join "`n").Trim()
}

$lines = [System.Collections.Generic.List[string]]::new()
function Say([string]$t) { $lines.Add($t) }

Say '===== FORGE AUDIT - ambiente di build ====='
Say ('Data: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm'))
Say ('-' * 60)

# OS ============================================================
Say '[OS]'
$os = Get-CimInstance Win32_OperatingSystem
Say ('OS: ' + $os.Caption + ' ' + $os.OSArchitecture)
Say ('Build: ' + $os.BuildNumber)
Say ('-' * 60)

# GPU ==========================================================
Say '[GPU]'
$smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($smi) {
    $smiOut = & nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -eq 0) { foreach ($g in $smiOut) { Say ('GPU: ' + $g) } }
    else { Say ('GPU: NVIDIA-smi presente ma errore: ' + ($smiOut -join ' | ')) }
} else {
    foreach ($g in (Get-CimInstance Win32_VideoController)) {
        Say ('GPU (WMI): ' + $g.Name + ' (VRAM da WMI puo sottostimare)')
    }
    Say 'GPU: NVIDIA-smi NON presente'
}
Say ('-' * 60)

# CUDA =========================================================
Say '[CUDA]'
$nvccOut = Get-CmdOut 'nvcc.exe' @('--version')
if ($nvccOut) {
    ($nvccOut -split "`n" | Select-Object -Last 2) | ForEach-Object { Say ('CUDA: ' + $_.Trim()) }
} else {
    $cudaDirs = Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' -Directory -ErrorAction SilentlyContinue
    if ($cudaDirs) { Say ('CUDA toolkit installato (nvcc non nel PATH): ' + ($cudaDirs.Name -join ', ')) }
    else { Say 'CUDA: nvcc assente, toolkit non trovato in Program Files' }
}
Say ('-' * 60)

# RAM e disco ==================================================
Say '[RAM e disco]'
$cs = Get-CimInstance Win32_ComputerSystem
Say ('RAM totale: ' + [math]::Round($cs.TotalPhysicalMemory/1GB, 1) + ' GB')
$d = Get-PSDrive C
Say ('Disco C:: libero ' + [math]::Round($d.Free/1GB, 1) + ' GB / ' + [math]::Round(($d.Used+$d.Free)/1GB, 1) + ' GB')
Say ('-' * 60)

# Strumenti ====================================================
Say '[Strumenti di build]'
$checks = @(
    @{n='gcc (WinLibs)'; e='gcc.exe';   a=@('--version')},
    @{n='clang';          e='clang.exe'; a=@('--version')},
    @{n='cmake';          e='cmake.exe'; a=@('--version')},
    @{n='python';         e='python.exe';a=@('--version')},
    @{n='node';           e='node.exe';  a=@('--version')},
    @{n='git';            e='git.exe';   a=@('--version')},
    @{n='ninja';          e='ninja.exe'; a=@('--version')}
)
foreach ($c in $checks) {
    $found = Get-Command $c.e -ErrorAction SilentlyContinue
    if ($found) {
        $out = (Get-CmdOut $c.e $c.a)
        $first = ($out -split "`n" | Select-Object -First 1).Trim()
        Say ($c.n + ': OK - ' + $found.Source + ' - ' + $first)
    } else {
        Say ($c.n + ': ASSENTE')
    }
}
if (-not (Get-Command python.exe -ErrorAction SilentlyContinue)) {
    $pyOut = Get-CmdOut 'py.exe' @('--version')
    if ($pyOut) { Say ('py launcher: OK - ' + ($pyOut -split "`n" | Select-Object -First 1).Trim()) }
}
Say ('-' * 60)

# MSVC =========================================================
Say '[MSVC / Visual Studio]'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -property installationName 2>&1 | ForEach-Object { "$_" }
    if ($vs) {
        Say ('Visual Studio rilevato: ' + ($vs -join ', '))
        # cerca cl.exe dentro le BuildTools (non nel PATH di default)
        $vsPath = & $vswhere -latest -property installationPath 2>&1 | Select-Object -First 1
        $cl = Get-ChildItem -Path (Join-Path $vsPath 'VC\Tools\MSVC') -Recurse -Filter 'cl.exe' -ErrorAction SilentlyContinue |
              Where-Object { $_.DirectoryName -match 'Hostx64\\x64$' } | Select-Object -First 1
        if ($cl) { Say ('cl.exe (MSVC x64): ' + $cl.FullName) } else { Say 'cl.exe: non trovato dentro VS (toolset C++ mancante?)' }
    } else {
        Say 'vswhere presente ma nessuna installazione VS'
    }
} else {
    Say 'Visual Studio (vswhere): NON rilevato'
}
Say ''
Say '(cl.exe e disponibile solo nel Developer Command Prompt; non nel PATH di default.)'

$lines -join "`r`n" | Set-Content -NoNewline -Encoding UTF8 "$env:TEMP\forge_audit_raw.txt"
Write-Host ($lines -join "`r`n")

