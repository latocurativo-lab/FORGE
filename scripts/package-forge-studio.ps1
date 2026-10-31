[CmdletBinding()]
param(
    [string] $OutputDirectory = 'artifacts\forge-studio-portable'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$studioRoot = Join-Path $repoRoot 'studio'
$agentBuild = Join-Path $repoRoot 'build-agent-studio'
$output = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))

if (Test-Path -LiteralPath $output) {
    throw "Output already exists; refusing to overwrite user files: $output"
}

$cmake = Get-Command cmake -ErrorAction Stop
& $cmake.Source -S (Join-Path $repoRoot 'agent') -B $agentBuild -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& $cmake.Source --build $agentBuild --config Release
if ($LASTEXITCODE -ne 0) { throw "FORGE backend/test build failed: $LASTEXITCODE" }
& (Get-Command ctest -ErrorAction Stop).Source --test-dir $agentBuild -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "FORGE CTest suite failed: $LASTEXITCODE" }

$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path $cargoBin) { $env:PATH = "$cargoBin;$env:PATH" }
Push-Location $studioRoot
try {
    npm run test
    if ($LASTEXITCODE -ne 0) { throw "Studio tests failed: $LASTEXITCODE" }
    $cargo = Get-Command cargo.exe -ErrorAction SilentlyContinue
    $cargoExe = if ($cargo) { $cargo.Source } else { Join-Path $cargoBin 'cargo.exe' }
    & $cargoExe test --locked --manifest-path src-tauri/Cargo.toml
    if ($LASTEXITCODE -ne 0) { throw "Tauri Rust tests failed: $LASTEXITCODE" }
    npm run tauri -- build
    if ($LASTEXITCODE -ne 0) { throw "Tauri release build failed: $LASTEXITCODE" }
}
finally { Pop-Location }

$backend = Join-Path $agentBuild 'Release\forge.exe'
$studioExe = Join-Path $studioRoot 'src-tauri\target\release\forge-studio.exe'
if (-not (Test-Path -LiteralPath $backend)) { throw "Backend output missing: $backend" }
if (-not (Test-Path -LiteralPath $studioExe)) { throw "Studio output missing: $studioExe" }

New-Item -ItemType Directory -Path $output | Out-Null
Copy-Item -LiteralPath $studioExe -Destination (Join-Path $output 'forge-studio.exe')
Copy-Item -LiteralPath $backend -Destination (Join-Path $output 'forge.exe')
Copy-Item -LiteralPath (Join-Path $studioRoot 'README.md') -Destination (Join-Path $output 'README-studio.md')
$manifest = Get-ChildItem -LiteralPath $output -File | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    [pscustomobject]@{ name = $_.Name; bytes = $_.Length; sha256 = $hash.Hash }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.json') -Encoding utf8
Write-Host "Portable FORGE Studio package created: $output"
Write-Host 'Run forge-studio.exe; forge.exe is resolved from the same directory.'
