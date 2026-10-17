param(
  [string]$BuildDir,
  [switch]$Open
)

# Builds the user manual (Sphinx) into <BuildDir>/manual.
# Needs uv (https://docs.astral.sh/uv/). The docs dependencies come from pyproject.toml.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$settings = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot
$resolvedBuildDir = $settings.BuildDir
if ($BuildDir) {
  $resolvedBuildDir = $BuildDir
}

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
  throw "uv is not installed. See https://docs.astral.sh/uv/ ."
}

$outputPath = Join-Path (Join-Path $repoRoot $resolvedBuildDir) "manual"
$sourcePath = Join-Path $repoRoot "manual"

Write-Host "Building the manual: $outputPath"
Push-Location $repoRoot
try {
  # -W turns warnings (broken links, missing files) into errors. -E rebuilds everything.
  uv run --group docs sphinx-build -b html -W --keep-going -E $sourcePath $outputPath
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
}

Write-Host "Done: $(Join-Path $outputPath 'index.html')"
if ($Open) {
  Start-Process (Join-Path $outputPath "index.html")
}
