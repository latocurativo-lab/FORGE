[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Low')]
param(
    [ValidateSet('Auto', 'CLI', 'Studio')]
    [string] $Mode = 'Auto',

    [string] $ModelPath = '',
    [string] $ServerPath = '',
    [string] $ForgePath = '',
    [string] $StudioPath = '',

    [ValidateRange(0, 131072)]
    [int] $ContextSize = 131072,
    [ValidateRange(0, 999)]
    [int] $GpuLayers = 99,
    [ValidateRange(1, 256)]
    [int] $Threads = 4,
    [ValidateRange(1, 16)]
    [int] $Parallel = 1,
    [ValidateRange(1, 3600)]
    [int] $HealthTimeoutSeconds = 180,

    [string] $ExpectedSha256 = '4dd9a27c68bd778ede351a553680a1a6e4ec62e5d383b549892860ea337be0c0',
    [long] $ExpectedBytes = 15440519296,
    [string] $ModelSource = 'https://huggingface.co/HauhauCS/Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive (IQ3_M)',

    [string[]] $ForgeArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-RepoPath([string] $Path) {
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $Path))
}

function Quote-WindowsArgument([string] $Value) {
    if ($Value -notmatch '[\s"]') { return $Value }
    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

function Get-ListenerProcess([int] $Port) {
    $connection = [System.Net.Sockets.TcpClient]::new()
    try {
        $async = $connection.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(500)) { return $null }
        $connection.EndConnect($async)
        if (-not $connection.Connected) { return $null }
    }
    catch { return $null }
    finally { $connection.Dispose() }

    $listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($listener) {
        $process = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
        if ($process) { return $process }
    }
    return [pscustomobject]@{ Id = 'unknown' }
}

function Test-ServerHealth {
    try {
        $response = Invoke-WebRequest -Uri 'http://127.0.0.1:18932/health' -TimeoutSec 2 -UseBasicParsing
        return ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300)
    }
    catch { return $false }
}

function Write-Plan([string] $SelectedMode, [string] $Model, [string] $Server, [string] $App, [string[]] $ServerArgs) {
    Write-Host ('Mode: {0}' -f $SelectedMode)
    Write-Host ('Model: {0}' -f $Model)
    Write-Host ('Model source: {0}' -f $ModelSource)
    Write-Host ('Expected SHA-256: {0} (metadata integrity check only; not a security verdict)' -f $ExpectedSha256)
    Write-Host ('Runtime: {0}' -f $Server)
    Write-Host ('Runtime args: {0}' -f (($ServerArgs | ForEach-Object { Quote-WindowsArgument $_ }) -join ' '))
    Write-Host ('Application: {0}' -f $App)
    if ($SelectedMode -eq 'CLI' -and $ForgeArguments.Count) { Write-Host ('CLI args: {0}' -f (($ForgeArguments | ForEach-Object { Quote-WindowsArgument $_ }) -join ' ')) }
    Write-Host 'Endpoint: http://127.0.0.1:18932'
}

try {
    $repoRoot = Resolve-RepoPath '..\..'
    if ([string]::IsNullOrWhiteSpace($ModelPath)) { $ModelPath = Join-Path $repoRoot 'models\Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ3_M.gguf' }
    if ([string]::IsNullOrWhiteSpace($ServerPath)) { $ServerPath = Join-Path $repoRoot 'runtime\upstream\llama.cpp\build\bin\Release\llama-server.exe' }
    if ([string]::IsNullOrWhiteSpace($ForgePath)) { $ForgePath = Join-Path $repoRoot 'build-agent\Release\forge.exe' }
    if ([string]::IsNullOrWhiteSpace($StudioPath)) { $StudioPath = Join-Path $repoRoot 'studio\src-tauri\target\release\forge-studio.exe' }
    $ModelPath = [System.IO.Path]::GetFullPath($ModelPath)
    $ServerPath = [System.IO.Path]::GetFullPath($ServerPath)
    $ForgePath = [System.IO.Path]::GetFullPath($ForgePath)
    $StudioPath = [System.IO.Path]::GetFullPath($StudioPath)

    if ($Mode -eq 'Auto') {
        $Mode = if (Test-Path -LiteralPath $StudioPath -PathType Leaf) { 'Studio' } else { 'CLI' }
    }
    if ($Mode -eq 'CLI') {
        $requiredCliOptions = @('--task', '--workspace', '--verify-exe', '--verify-args', '--allow-exe')
        $missingCliOption = $false
        foreach ($option in $requiredCliOptions) {
            $optionIndex = [Array]::IndexOf($ForgeArguments, $option)
            if ($optionIndex -lt 0 -or $optionIndex + 1 -ge $ForgeArguments.Count -or [string]::IsNullOrWhiteSpace($ForgeArguments[$optionIndex + 1])) {
                $missingCliOption = $true
                break
            }
        }
        if ($ForgeArguments.Count -eq 0 -or $ForgeArguments[0] -ne 'run' -or $missingCliOption) {
            throw "CLI mode requires 'run' with --task, --workspace, --verify-exe, --verify-args, and at least one --allow-exe; forge.exe without arguments enters JSONL stdin mode. Example: .\scripts\launch\forge-local.ps1 -Mode CLI -ForgeArguments @('run', '--task', 'Implement the change', '--workspace', 'C:\work\fixture', '--verify-exe', 'C:\tools\ctest.exe', '--verify-args', '--test-dir build --output-on-failure', '--allow-exe', 'C:\tools\ctest.exe')."
        }
    }
    $appPath = if ($Mode -eq 'Studio') { $StudioPath } else { $ForgePath }
    $chatTemplatePath = Join-Path $repoRoot 'models\qwen3.6-chat-template.community.jinja'
    if (-not (Test-Path -LiteralPath $chatTemplatePath -PathType Leaf)) { throw "Missing Qwen chat template: $chatTemplatePath" }
    $serverArgs = @(
        '-m', $ModelPath, '-ngl', "$GpuLayers", '-c', "$ContextSize", '-t', "$Threads", '-np', "$Parallel",
        '--host', '127.0.0.1', '--port', '18932', '--jinja', '--chat-template-file', $chatTemplatePath,
        '--chat-template-kwargs', '{"preserve_thinking":true}',
        '--reasoning-format', 'deepseek', '--reasoning', 'on',
        '--cache-type-k', 'q8_0', '--cache-type-v', 'q8_0', '--fit', 'on',
        '--temp', '0.6', '--top-p', '0.95', '--top-k', '20', '--min-p', '0.0', '--repeat-penalty', '1.0'
    )

    Write-Host ('FORGE local launcher - repo {0}' -f $repoRoot)
    if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'ExpectedSha256 must be a 64-character hexadecimal SHA-256 value.'
    }
    if ([string]::IsNullOrWhiteSpace($ModelSource)) { throw 'ModelSource must identify the model provenance.' }

    if ($WhatIfPreference) {
        Write-Plan $Mode $ModelPath $ServerPath $appPath $serverArgs
        Write-Host 'WhatIf: no file hashing, process start, network probe, or other side effect performed.'
        return
    }

    foreach ($required in @(@{ Path = $ModelPath; Name = 'model' }, @{ Path = $ServerPath; Name = 'llama-server' }, @{ Path = $appPath; Name = $Mode })) {
        if (-not (Test-Path -LiteralPath $required.Path -PathType Leaf)) { throw ('Missing {0} file: {1}. No download or installation is attempted.' -f $required.Name, $required.Path) }
    }

    $model = Get-Item -LiteralPath $ModelPath
    if ($model.Length -ne $ExpectedBytes) { throw ('Model size mismatch: got {0} bytes, expected {1} from models/MODEL.lock.' -f $model.Length, $ExpectedBytes) }
    $actualSha256 = (Get-FileHash -LiteralPath $ModelPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $ExpectedSha256.ToLowerInvariant()) { throw ('Model SHA-256 mismatch: got {0}; expected {1}.' -f $actualSha256, $ExpectedSha256) }
    Write-Host ('Model verified: {0} bytes, SHA-256 {1}; source {2}' -f $model.Length, $actualSha256, $ModelSource)
    Write-Host 'Checksum/source verification is provenance/integrity metadata only, not a model safety or security assessment.'

    $serverProcess = $null
    try {
    $existingListener = Get-ListenerProcess 18932
    if ($existingListener) {
        if (-not (Test-ServerHealth)) { throw ('Port 18932 is already owned by PID {0}, but its /health endpoint did not pass. Refusing to replace or stop it.' -f $existingListener.Id) }
        Write-Host ('Using pre-existing healthy loopback server (PID {0}); launcher will not stop it.' -f $existingListener.Id)
    }
    else {
        Write-Plan $Mode $ModelPath $ServerPath $appPath $serverArgs
        if (-not $PSCmdlet.ShouldProcess($ServerPath, 'Start llama-server on 127.0.0.1:18932')) { return }
        $serverProcess = Start-Process -FilePath $ServerPath -ArgumentList ($serverArgs | ForEach-Object { Quote-WindowsArgument $_ }) -WorkingDirectory $repoRoot -PassThru
        Write-Host ('Started llama-server PID {0}; waiting up to {1} seconds for /health...' -f $serverProcess.Id, $HealthTimeoutSeconds)
        $deadline = [DateTime]::UtcNow.AddSeconds($HealthTimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($serverProcess.HasExited) { throw ('llama-server exited early with code {0}.' -f $serverProcess.ExitCode) }
            if (Test-ServerHealth) { break }
            Start-Sleep -Seconds 1
        }
        if (-not (Test-ServerHealth)) { throw ('llama-server did not become healthy within {0} seconds.' -f $HealthTimeoutSeconds) }
        Write-Host 'llama-server health check passed.'
    }

    if (-not $PSCmdlet.ShouldProcess($appPath, ('Launch FORGE {0}' -f $Mode))) { return }
    $appArgs = @($ForgeArguments | ForEach-Object { Quote-WindowsArgument $_ })
    if ($appArgs.Count -eq 0) {
        $appProcess = Start-Process -FilePath $appPath -WorkingDirectory $repoRoot -PassThru -Wait
    }
    else {
        $appProcess = Start-Process -FilePath $appPath -ArgumentList $appArgs -WorkingDirectory $repoRoot -PassThru -Wait
    }
    if ($appProcess.ExitCode -ne 0) { throw ('FORGE {0} exited with code {1}.' -f $Mode, $appProcess.ExitCode) }
    }
    finally {
        if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
            Write-Host ('Stopping only launcher-owned llama-server PID {0}.' -f $serverProcess.Id)
            Stop-Process -Id $serverProcess.Id -ErrorAction SilentlyContinue
            $serverProcess.WaitForExit(10000) | Out-Null
        }
    }
}
catch {
    Write-Error ('FORGE launcher failed: {0}' -f $_.Exception.Message)
    exit 1
}
