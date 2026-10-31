# Compatibility entry point. The old llama-cli/filler harness did not prove
# populated token contexts and lacked a pre-eval VRAM guard. Delegate to the
# tokenizer-verified Python harness instead.
param(
  [Parameter(Mandatory=$true)][string]$Model,
  [int]$NGen = 16,
  [int]$Ctx = 4096,
  [string]$PromptFile = "",
  [string]$Cli = "C:\Users\geekandhack\Desktop\FORGE\runtime\upstream\llama.cpp\build\bin\Release\llama-server.exe",
  [string]$OutDir = "C:\Users\geekandhack\Desktop\FORGE\artifacts\bench"
)

$ErrorActionPreference = 'Stop'
if ($PromptFile -ne '') {
  throw 'Custom PromptFile is disabled: contexts must be deterministically generated and tokenized by llama.cpp.'
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runner = Join-Path $PSScriptRoot 'bench_context.py'
$arguments = @(
  $runner,
  '--model', $Model,
  '--server', $Cli,
  '--out', $OutDir,
  '--targets', [string]$Ctx,
  '--replicates', '3',
  '--max-tokens', [string]$NGen
)
Push-Location $root
try {
  & python @arguments
  $code = $LASTEXITCODE
} finally {
  Pop-Location
}
exit $code
