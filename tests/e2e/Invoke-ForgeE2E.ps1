param(
    [string] $ForgeExe = (Join-Path $PSScriptRoot '..\..\build-agent\Release\forge.exe'),
    [string] $RuntimeExe = (Join-Path $PSScriptRoot '..\..\runtime\upstream\llama.cpp\build\bin\Release\llama-server.exe'),
    [string] $ModelPath = (Join-Path $PSScriptRoot '..\..\models\gemma-4-12b-it-Q4_K_M.gguf'),
    [string] $Workspace = (Join-Path $env:TEMP 'forge-e2e-091266db33784cdd84ea9625691ccc9b\workspace'),
    [string] $VerifierExe = '',
    [string] $ExpectedProtocolMarker = 'approval_required'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:utf8NoBom = [Text.UTF8Encoding]::new($false)
$fixtureRoot = (Get-Item -LiteralPath $Workspace -ErrorAction Stop).FullName
$logRoot = Join-Path (Split-Path -Parent $fixtureRoot) 'run'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$jsonlPath = Join-Path $logRoot 'forge.stdout.jsonl'
$stdinPath = Join-Path $logRoot 'forge.stdin.jsonl'
$stdinBytesPath = Join-Path $logRoot 'forge.stdin.bytes'
$stderrPath = Join-Path $logRoot 'forge.stderr.log'
$runtimeOut = Join-Path $logRoot 'llama-server.stdout.log'
$runtimeErr = Join-Path $logRoot 'llama-server.stderr.log'
$checkpoint = Join-Path (Split-Path -Parent $fixtureRoot) 'agent-checkpoint.json'
$expectedFiles = @('src\sum.js', 'test\sum.test.js')
$startedAt = [DateTimeOffset]::UtcNow
$script:approvalRows = [Collections.Generic.List[object]]::new()
$script:toolRows = [Collections.Generic.List[object]]::new()
$script:seenRequestIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$script:seenToolCallIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$script:expectedExecDenyIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$script:pendingRequestId = $null
$script:verifierOutput = $null
$script:verificationPassed = $false
$script:failClosed = $false
$script:cancelSent = $false
$script:server = $null
$script:serverPid = 0
$script:serverStopConfirmed = $false
$script:forge = $null
$script:forgeExitCode = $null

function Resolve-ExistingFile([string] $Path, [string] $Label) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer) { throw "$Label is not a file: $($item.FullName)" }
    return $item.FullName
}

function Send-JsonLine([Diagnostics.Process] $Process, [object] $Value) {
    $jsonLine = ConvertTo-Json -InputObject $Value -Compress -Depth 16
    Add-Content -LiteralPath $script:stdinPath -Encoding utf8 -Value $jsonLine
    # Write explicit UTF-8 payload bytes without console-codepage conversion.
    # Process.StandardInput may add its own UTF-8 preamble on some Windows .NETs;
    # forge.exe accepts that optional BOM on the first JSONL record.
    $bytes = $script:utf8NoBom.GetBytes($jsonLine + "`n")
    $capture = [IO.FileStream]::new($script:stdinBytesPath, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $capture.Write($bytes, 0, $bytes.Length); $capture.Flush($true) } finally { $capture.Dispose() }
    $Process.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $Process.StandardInput.BaseStream.Flush()
}

function Send-Cancel([Diagnostics.Process] $Process) {
    if ($Process -and -not $Process.HasExited -and -not $script:cancelSent) {
        $script:cancelSent = $true
        Send-JsonLine $Process @{ type = 'cancel' }
    }
}

function Get-ExactApproval([object] $Event) {
    $tool = [string]$Event.tool
    $cap = [string]$Event.capability
    $mutation = [bool]$Event.mutation
    $toolArgs = $Event.arguments
    $approved = $false
    $reason = 'default deny'

    if ($cap -eq 'VERIFY' -and $tool -eq 'run_command') {
        $approved = $false
        if ($Event.tool_call_id -ceq 'verification' -and $toolArgs -and [string]$toolArgs.executable -ieq $script:verifierPath -and [string]$toolArgs.args -ceq $script:verifyArgs -and $mutation -eq $true -and -not $script:verificationApproved) {
            $approved = $true
            $script:verificationApproved = $true
            $reason = 'exact native fixture verifier + exact args; separate VERIFY approval'
        } else { $reason = 'VERIFY mismatch, duplicate, or not mutation-marked' }
        return @{ approved = $approved; reason = $reason }
    }

    if ($tool -eq 'run_command') { return @{ approved = $false; reason = 'all model-requested EXEC denied; verifier only via VERIFY' } }
    if ($mutation -and $tool -notin @('write_file', 'replace_file')) {
        return @{ approved = $false; reason = 'unexpected mutation flag on non-write tool' }
    }
    $path = if ($toolArgs -and $toolArgs.path -is [string]) { ([string]$toolArgs.path).Replace('\', '/') } else { $null }
    switch ($tool) {
        'read_file' {
            if ($cap -eq 'READ' -and $path -cin @('task.md', 'package.json', 'src/sum.js', 'test/sum.test.js')) { $approved = $true; $reason = 'fixture read path allowlist' }
        }
        'search_file' {
            if ($cap -eq 'READ' -and $path -cin @('task.md', 'package.json', 'src/sum.js', 'test/sum.test.js') -and $toolArgs.needle -is [string] -and $toolArgs.needle.Length -gt 0) { $approved = $true; $reason = 'fixture search path allowlist' }
        }
        'list_dir' {
            if ($cap -eq 'LIST' -and $path -cin @('', 'src', 'test')) { $approved = $true; $reason = 'fixture directory allowlist' }
        }
        'write_file' {
            if ($cap -eq 'WRITE' -and $mutation -and $path -cin @('src/sum.js', 'test/sum.test.js')) {
                $destination = Join-Path $script:fixtureRoot ($path -replace '/', '\')
                if (-not (Test-Path -LiteralPath $destination)) { $approved = $true; $reason = 'create-only write to expected fixture output' }
                else { $reason = 'expected output already exists; refusing overwrite' }
            }
        }
        'replace_file' {
            if ($cap -eq 'WRITE' -and $mutation -and $path -cin @('src/sum.js', 'test/sum.test.js') -and $toolArgs.expected -is [string] -and $toolArgs.content -is [string]) {
                $destination = Join-Path $script:fixtureRoot ($path -replace '/', '\')
                if (Test-Path -LiteralPath $destination -PathType Leaf) { $approved = $true; $reason = 'exact-content replacement confined to expected fixture output' }
                else { $reason = 'replacement target does not exist in fixture' }
            }
        }
        default { $reason = 'unrecognized tool denied' }
    }
    return @{ approved = $approved; reason = $reason }
}

function Handle-Event([string] $Line, [Diagnostics.Process] $Process) {
    Add-Content -LiteralPath $jsonlPath -Encoding utf8 -Value $Line
    try { $event = $Line | ConvertFrom-Json -ErrorAction Stop } catch {
        $script:failClosed = $true
        Send-Cancel $Process
        throw "Malformed stdout JSONL: $Line"
    }
    $type = [string]$event.type
    if ($type -eq 'approval_required') {
        if ($event.request_id -isnot [string] -or $event.request_id -cnotmatch '^forge-[0-9]+-[0-9]+$' -or
            $event.tool_call_id -isnot [string] -or [string]::IsNullOrWhiteSpace($event.tool_call_id) -or
            $event.tool -isnot [string] -or [string]::IsNullOrWhiteSpace($event.tool) -or
            $event.capability -isnot [string] -or [string]::IsNullOrWhiteSpace($event.capability) -or
            $event.mutation -isnot [bool] -or $event.arguments -isnot [System.Management.Automation.PSCustomObject] -or
            $script:pendingRequestId) {
            $script:failClosed = $true
            throw 'Malformed approval_required event; no operation authorized.'
        }
        if (-not $script:seenRequestIds.Add($event.request_id)) {
            $script:failClosed = $true
            throw "Duplicate approval request_id '$($event.request_id)'; no operation authorized."
        }
        if (-not $script:seenToolCallIds.Add($event.tool_call_id)) {
            $script:failClosed = $true
            throw "Duplicate tool_call_id '$($event.tool_call_id)'; no operation authorized."
        }
        $script:pendingRequestId = $event.request_id
        $decision = Get-ExactApproval $event
        $script:approvalRows.Add([pscustomobject]@{
            at = [DateTimeOffset]::UtcNow.ToString('o'); request_id = [string]$event.request_id
            tool_call_id = [string]$event.tool_call_id; tool = [string]$event.tool
            capability = [string]$event.capability; mutation = [bool]$event.mutation
            approved = [bool]$decision.approved; reason = [string]$decision.reason
            arguments = $event.arguments
        })
        if ($event.capability -eq 'VERIFY' -and $decision.approved) { $script:verifierApprovalRow = $script:approvalRows[$script:approvalRows.Count - 1] }
        if ($event.mutation -eq $true -and $decision.approved) { $script:mutationApprovals++ }
        if (-not $decision.approved -and $event.capability -eq 'EXEC' -and $event.tool -eq 'run_command') { [void]$script:expectedExecDenyIds.Add([string]$event.tool_call_id) }
        Send-JsonLine $Process @{ type = 'approval'; request_id = [string]$event.request_id; approved = [bool]$decision.approved }
        $script:pendingRequestId = $null
        if (-not $decision.approved -and -not ($event.capability -eq 'EXEC' -and $event.tool -eq 'run_command')) {
            $script:failClosed = $true
            Send-Cancel $Process
        }
        return
    }
    if ($type -in @('tool_request', 'tool_result', 'tool_denied', 'tool_failure')) {
        $script:toolRows.Add([pscustomobject]@{ at = [DateTimeOffset]::UtcNow.ToString('o'); type = $type; tool = [string]$event.tool; tool_call_id = [string]$event.tool_call_id; message = [string]$event.message })
        $expectedExecDeny = $event.tool -eq 'run_command' -and $event.tool_call_id -is [string] -and $script:expectedExecDenyIds.Contains([string]$event.tool_call_id)
        if ($type -in @('tool_denied', 'tool_failure') -and -not $expectedExecDeny) { $script:failClosed = $true; Send-Cancel $Process }
    }
    if ($type -eq 'verification_passed') { $script:verificationPassed = $true; $script:verifierOutput = [string]$event.message }
    if ($type -in @('error', 'incomplete', 'verification_failed', 'cancelled')) { $script:failClosed = $true; Send-Cancel $Process }
    if ($type -eq 'exit') { $script:exitState = [string]$event.message }
}

$forgePath = Resolve-ExistingFile $ForgeExe 'forge.exe'
$runtimePath = Resolve-ExistingFile $RuntimeExe 'llama-server.exe'
$modelPath = Resolve-ExistingFile $ModelPath 'Q4_K_M model'
$tempRoot = (Get-Item -LiteralPath $env:TEMP -ErrorAction Stop).FullName.TrimEnd('\') + '\'
$fixtureParent = Split-Path -Parent $fixtureRoot
if (-not $fixtureRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $fixtureRoot) -cne 'workspace' -or
    (Split-Path -Leaf $fixtureParent) -cnotmatch '^forge-e2e-[0-9a-f]{32}$') {
    throw 'Workspace must be a workspace directory under a unique forge-e2e-<guid> temp root.'
}
$expectedWorkspace = $fixtureRoot
$expectedVerifier = (Get-Item -LiteralPath (Join-Path (Split-Path -Parent $expectedWorkspace) 'verifiers\verify_fixture.exe') -ErrorAction Stop).FullName
if (-not $fixtureRoot.Equals($expectedWorkspace, [StringComparison]::OrdinalIgnoreCase)) { throw 'Workspace must be the prepared temporary fixture, not an arbitrary temp path.' }
$verifierCandidate = if ([string]::IsNullOrWhiteSpace($VerifierExe)) { $expectedVerifier } else { $VerifierExe }
$script:verifierPath = Resolve-ExistingFile $verifierCandidate 'native fixture verifier'
if ([IO.Path]::GetExtension($script:verifierPath) -ine '.exe') { throw 'Verifier must be a native .exe; no interpreters or shells.' }
if (-not $script:verifierPath.Equals($expectedVerifier, [StringComparison]::OrdinalIgnoreCase)) { throw 'Only the native verifier prepared beside this fixture may be allowlisted.' }
if (Test-Path -LiteralPath $checkpoint) { throw 'Existing checkpoint found; JSONL has no resume message, refusing to overwrite or replay it.' }
foreach ($relative in $expectedFiles) {
    if (Test-Path -LiteralPath (Join-Path $fixtureRoot $relative)) { throw "Existing fixture output '$relative'; refusing overwrite/replay." }
}
$agentReadme = Get-Content -LiteralPath (Join-Path $repo 'agent\README.md') -Raw
if (-not $agentReadme.Contains($ExpectedProtocolMarker)) { throw "Protocol marker '$ExpectedProtocolMarker' missing; no process started." }
$script:verifyArgs = "--workspace `"$fixtureRoot`""
$script:verificationApproved = $false
$script:verifierApprovalRow = $null
$script:mutationApprovals = 0
$script:exitState = $null

# Use a clean JSONL/report log per run. The persistent fixture and checkpoint
# are not reset: a non-safe checkpoint must never be replayed automatically.
Remove-Item -LiteralPath $jsonlPath, $stdinPath, $stdinBytesPath, $stderrPath, $runtimeOut, $runtimeErr -Force -ErrorAction SilentlyContinue
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 18932)
try { $listener.Start(); $listener.Stop() } catch { throw "127.0.0.1:18932 is occupied; refusing to use another endpoint. $($_.Exception.Message)" }

$script:runtimeOut = $runtimeOut
$script:runtimeErr = $runtimeErr
$script:stdinPath = $stdinPath
$script:stdinBytesPath = $stdinBytesPath
$runtimeArgs = "-m `"$modelPath`" -ngl 99 -fa on -c 4096 -t 4 -np 1 --host 127.0.0.1 --port 18932 --jinja --reasoning off"
try {
    $script:server = Start-Process -FilePath $runtimePath -ArgumentList $runtimeArgs -PassThru -NoNewWindow -RedirectStandardOutput $runtimeOut -RedirectStandardError $runtimeErr
    $script:serverPid = $script:server.Id
    $ready = $false
    $readyUntil = [DateTimeOffset]::UtcNow.AddSeconds(180)
    while ([DateTimeOffset]::UtcNow -lt $readyUntil -and -not $script:server.HasExited) {
        try {
            $health = Invoke-WebRequest -Uri 'http://127.0.0.1:18932/health' -TimeoutSec 2 -UseBasicParsing
            if ($health.StatusCode -ge 200 -and $health.StatusCode -lt 300) { $ready = $true; break }
        } catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready) { throw 'llama-server /health did not become ready on 127.0.0.1:18932 within 180 seconds.' }

    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $forgePath
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    if ($psi.PSObject.Properties['StandardInputEncoding']) { $psi.StandardInputEncoding = $script:utf8NoBom }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $script:forge = [Diagnostics.Process]::new()
    $script:forge.StartInfo = $psi
    if (-not $script:forge.Start()) { throw 'Could not start forge.exe.' }
    $stdoutTask = $script:forge.StandardOutput.ReadLineAsync()
    $stderrTask = $script:forge.StandardError.ReadLineAsync()

    $task = (Get-Content -LiteralPath (Join-Path $fixtureRoot 'task.md') -Raw).Trim()
    $startMessage = @{
        type = 'start'; task = $task; workspace = $fixtureRoot; base_url = 'http://127.0.0.1:18932'
        checkpoint = $checkpoint; allow_exe = @($script:verifierPath)
        verifier = @{ executable = $script:verifierPath; args = $script:verifyArgs }
    }
    Send-JsonLine $script:forge $startMessage
    $overallUntil = [DateTimeOffset]::UtcNow.AddMinutes(30)
    $stdoutEnded = $false
    $stderrEnded = $false
    while ([DateTimeOffset]::UtcNow -lt $overallUntil -and -not $script:forge.HasExited) {
        if (-not $stdoutEnded -and $stdoutTask.Wait(50)) {
            $line = $stdoutTask.Result
            if ($null -eq $line) { $stdoutEnded = $true }
            else { Handle-Event $line $script:forge; $stdoutTask = $script:forge.StandardOutput.ReadLineAsync() }
        }
        if (-not $stderrEnded -and $stderrTask.Wait(0)) {
            $err = $stderrTask.Result
            if ($null -eq $err) { $stderrEnded = $true }
            else { Add-Content -LiteralPath $stderrPath -Encoding utf8 -Value "[$([DateTimeOffset]::UtcNow.ToString('o'))] $err"; $stderrTask = $script:forge.StandardError.ReadLineAsync() }
        }
        if ($script:failClosed) { Send-Cancel $script:forge }
    }
    if (-not $script:forge.HasExited) { $script:failClosed = $true; Send-Cancel $script:forge; Start-Sleep -Seconds 2; if (-not $script:forge.HasExited) { $script:forge.Kill($true) }; throw 'Forge exceeded 30-minute task timeout; process tree terminated.' }
    $script:forge.WaitForExit()
    while (-not $stdoutEnded -and $stdoutTask.Wait(2000)) {
        $finalLine = $stdoutTask.Result
        if ($null -eq $finalLine) { $stdoutEnded = $true } else { Handle-Event $finalLine $script:forge; $stdoutTask = $script:forge.StandardOutput.ReadLineAsync() }
    }
    while (-not $stderrEnded -and $stderrTask.Wait(2000)) {
        $lastErr = $stderrTask.Result
        if ($null -eq $lastErr) { $stderrEnded = $true } else { Add-Content -LiteralPath $stderrPath -Encoding utf8 -Value "[$([DateTimeOffset]::UtcNow.ToString('o'))] $lastErr"; $stderrTask = $script:forge.StandardError.ReadLineAsync() }
    }
    $script:forgeExitCode = $script:forge.ExitCode
    if ($script:forgeExitCode -ne 0 -or -not $script:exitState) { $script:failClosed = $true }
} finally {
    if ($script:forge -and -not $script:forge.HasExited) { Send-Cancel $script:forge; Start-Sleep -Milliseconds 500; if (-not $script:forge.HasExited) { $script:forge.Kill($true) } }
    if ($script:serverPid -ne 0) {
        $serverProcess = Get-Process -Id $script:serverPid -ErrorAction SilentlyContinue
        if ($serverProcess) { Stop-Process -Id $script:serverPid -Force }
        if ($script:server) { try { $script:server.WaitForExit(5000) } catch { } }
        $script:serverStopConfirmed = -not (Get-Process -Id $script:serverPid -ErrorAction SilentlyContinue)
    }
}

$finishedAt = [DateTimeOffset]::UtcNow
$fileEvidence = foreach ($relative in $expectedFiles) {
    $full = Join-Path $fixtureRoot $relative
    if (Test-Path -LiteralPath $full -PathType Leaf) { Get-FileHash -LiteralPath $full -Algorithm SHA256 | Select-Object @{n='path';e={$relative}}, Hash, @{n='bytes';e={(Get-Item -LiteralPath $full).Length}} }
    else { [pscustomobject]@{ path = $relative; Hash = $null; bytes = 0 } }
}
$checkpointEvidence = if (Test-Path -LiteralPath $checkpoint) { Get-Content -LiteralPath $checkpoint -Raw | ConvertFrom-Json } else { $null }
$runtimeExit = if ($script:server) { $script:server.ExitCode } else { $null }
$report = [pscustomobject]@{
    started_utc = $startedAt.ToString('o'); finished_utc = $finishedAt.ToString('o'); elapsed_seconds = [math]::Round(($finishedAt - $startedAt).TotalSeconds, 3)
    protocol_marker = $ExpectedProtocolMarker; forge_exe = $forgePath; forge_exit_code = $script:forgeExitCode
    exit_state = $script:exitState; fail_closed = $script:failClosed; verification_passed = $script:verificationPassed
    verifier_approval = $script:verifierApprovalRow; verifier_output = $script:verifierOutput
    separately_recorded_mutation_approvals = $script:mutationApprovals; approvals = @($script:approvalRows)
    tool_event_count = $script:toolRows.Count; tool_events = @($script:toolRows); fixture_files = @($fileEvidence)
    checkpoint = $checkpoint; checkpoint_state = $checkpointEvidence; runtime_endpoint = '127.0.0.1:18932'
    runtime_args = '-ngl 99 -fa on -c 4096 -t 4 -np 1 --host 127.0.0.1 --port 18932 --jinja --reasoning off'
    runtime_pid = $script:serverPid; runtime_exit_code_after_stop = $runtimeExit; runtime_stop_confirmed = $script:serverStopConfirmed
    forge_stdout_jsonl_created = (Test-Path -LiteralPath $jsonlPath)
    resume = 'not attempted: JSONL start API has no resume message; unsafe checkpoints are never replayed'
    tool_call_count = @($script:toolRows | Where-Object type -eq 'tool_request').Count
    expected_model_exec_denials = $script:expectedExecDenyIds.Count
    logs = @($stdinPath, $jsonlPath, $stderrPath, $runtimeOut, $runtimeErr)
}
$reportPath = Join-Path $logRoot 'e2e-summary.json'
$report | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $reportPath -Encoding utf8
if ($script:forgeExitCode -ne 0 -or $script:exitState -ne 'COMPLETED' -or -not $script:verificationPassed -or $script:failClosed -or -not $script:verificationApproved) {
    throw "E2E did not complete cleanly; see $reportPath and JSONL/log files. No success claimed."
}
Write-Output "E2E PASS: $reportPath"
