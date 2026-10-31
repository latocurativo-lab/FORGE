[CmdletBinding()]
param(
    [ValidateSet('Abliterated', 'Base')]
    [string] $ModelVariant = 'Abliterated',
    # 0 = let --fit on auto-balance; otherwise forced value. Forcing a large value with
    # --fit on makes the fit abort and causes PCIe spilling (observed 1.7 tok/s).
    [ValidateRange(0, 131072)]
    [int] $ContextSize = 0,
    [ValidateRange(0, 256)]
    [int] $Threads = 0,
    [ValidateRange(0, 99)]
    [int] $GpuLayers = 0, # 0 = llama.cpp default 'auto'
    [ValidateRange(0, 256)]
    [int] $CpuMoe = 0, # 0 = experts stay on GPU with the rest of the layers
    [ValidateRange(0, 8192)]
    [int] $MoeCacheMib = 0, # MiB of hot expert weights kept cached on GPU despite --n-cpu-moe
    [ValidateRange(1, 3600)]
    [int] $HealthTimeoutSeconds = 900
)

# Variant defaults tuned for RTX 4060 Ti 16 GB + 31 GB system RAM (measured free < 8 GiB with apps).
# Expert weights ≈ 0.27 GiB/layer (IQ3_M); KV q8_0 ≈ 96 KiB/token (n_layer 48).
$variantDefaults = @{
    Abliterated = @{ ContextSize = 65536; Threads = 4; CpuMoe = 25; MoeCacheMib = 1024 }
    Base        = @{ ContextSize = 65536; Threads = 4; CpuMoe = 20; MoeCacheMib = 1024 }
}
foreach ($key in @('ContextSize', 'Threads', 'CpuMoe', 'MoeCacheMib')) {
    if ($PSBoundParameters.ContainsKey($key)) { continue }
    Set-Variable -Name $key -Value $variantDefaults[$ModelVariant][$key]
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Quote-WindowsArgument([string] $Value) {
    if ($Value -notmatch '[\s"]') { return $Value }
    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$models = @{
    Abliterated = @{
        File = 'Qwen3.6-35B-A3B-Uncensored-HauhauCS-Aggressive-IQ3_M.gguf'
        Bytes = 15440519296L
        Sha256 = '4dd9a27c68bd778ede351a553680a1a6e4ec62e5d383b549892860ea337be0c0'
    }
    Base = @{
        File = 'Qwen3.6-35B-A3B-UD-IQ3_XXS.gguf'
        Bytes = 13211155424L
        Sha256 = '9c964e657212fea1f24905dd7b0a89b82fd807d19fab0b41da14251b07b88fbe'
    }
}
$selectedModel = $models[$ModelVariant]
$modelPath = Join-Path $repoRoot (Join-Path 'models' $selectedModel.File)
$templatePath = Join-Path $repoRoot 'models\qwen3.6-chat-template.community.jinja'
$serverPath = Join-Path $repoRoot 'runtime\upstream\llama.cpp\build\bin\Release\llama-server.exe'
$expectedBytes = $selectedModel.Bytes
$expectedSha256 = $selectedModel.Sha256
$port = 18932

foreach ($required in @($modelPath, $templatePath, $serverPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file not found: $required" }
}
$model = Get-Item -LiteralPath $modelPath
if ($model.Length -ne $expectedBytes) { throw "Qwen GGUF size mismatch: $($model.Length) bytes" }
$actualSha = (Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha -ne $expectedSha256) { throw "Qwen GGUF SHA-256 mismatch: $actualSha" }

$listener = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
if ($listener) {
    $existing = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)"
    $knownModel = $existing.CommandLine -match 'Qwen3\.6-35B-A3B-(UD-IQ3_XXS|Uncensored-HauhauCS-Aggressive-IQ3_M)\.gguf|gemma-4-12b-it-Q4_K_M\.gguf'
    if ($existing.Name -notmatch '^llama-server(\.exe)?$' -or -not $knownModel) {
        throw "Port $port is occupied by an unexpected process (PID $($listener.OwningProcess)); refusing to stop it."
    }
    Write-Host "Stopping previous verified FORGE llama-server PID $($listener.OwningProcess)."
    Stop-Process -Id $listener.OwningProcess -Force
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline -and (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)) {
        Start-Sleep -Milliseconds 250
    }
    if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) { throw "Port $port did not become free after stopping the verified Gemma server." }
}

$serverArgs = @(
    '-m', $modelPath,
    '-c', "$ContextSize", '-t', "$Threads", '-np', '1',
    '--host', '127.0.0.1', '--port', "$port", '--jinja', '--chat-template-file', $templatePath,
    '--chat-template-kwargs', '{"preserve_thinking":true}',
    '--reasoning-format', 'deepseek', '--reasoning', 'on',
    '--cache-type-k', 'q8_0', '--cache-type-v', 'q8_0', '--fit', 'on',
    '--temp', '0.6', '--top-p', '0.95', '--top-k', '20', '--min-p', '0.0', '--repeat-penalty', '1.0'
)
if ($GpuLayers -gt 0) { $serverArgs += @('-ngl', "$GpuLayers") }
if ($CpuMoe -gt 0) { $serverArgs += @('--n-cpu-moe', "$CpuMoe") }
if ($MoeCacheMib -gt 0) { $serverArgs += @('--moe-cache-mib', "$MoeCacheMib") }
$argsString = ($serverArgs | ForEach-Object { Quote-WindowsArgument $_ }) -join ' '
$logPrefix = if ($ModelVariant -eq 'Abliterated') { 'qwen-uncensored' } else { 'qwen-server' }
$stdout = Join-Path $repoRoot "runtime\$logPrefix.stdout.log"
$stderr = Join-Path $repoRoot "runtime\$logPrefix.stderr.log"
$process = Start-Process -FilePath $serverPath -ArgumentList $argsString -WorkingDirectory $repoRoot -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
Write-Host "Started Qwen3.6 $ModelVariant llama-server PID $($process.Id) with $ContextSize context."

$deadline = [DateTime]::UtcNow.AddSeconds($HealthTimeoutSeconds)
while ([DateTime]::UtcNow -lt $deadline) {
    $process.Refresh()
    if ($process.HasExited) {
        $tail = if (Test-Path $stderr) { (Get-Content $stderr -Tail 60) -join "`n" } else { '(no server stderr log)' }
        throw "Qwen llama-server exited with code $($process.ExitCode).`n$tail"
    }
    try {
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$port/health" -TimeoutSec 3
        if ($health.status -eq 'ok' -or $health.Status -eq 'ok') {
            Write-Host "Qwen3.6 $ModelVariant server healthy at http://127.0.0.1:$port/v1 (PID $($process.Id))."
            exit 0
        }
    }
    catch { }
    Start-Sleep -Seconds 2
}
throw "Qwen llama-server did not become healthy within $HealthTimeoutSeconds seconds. See $stderr"
