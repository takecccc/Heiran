param(
  [string]$BuildDir,
  [string]$Config,
  [string]$SessionPath,
  # Measure interaction performance and write the report to this path.
  [string]$ProfileLog,
  # Draw as soon as input arrives instead of waiting for the display refresh.
  [switch]$NoDisplaySync
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$settings = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot
$resolvedBuildDir = $settings.BuildDir
$resolvedConfig = $settings.BuildConfig

if ($BuildDir) {
  $resolvedBuildDir = $BuildDir
}
if ($Config) {
  $resolvedConfig = $Config
}

$binaryPath = Join-Path $repoRoot "$resolvedBuildDir/heiran.exe"
if (-not (Test-Path $binaryPath)) {
  throw "Executable was not found: $binaryPath"
}

$arguments = @()
if ($SessionPath) {
  $arguments += $SessionPath
}

Write-Host "Running: $binaryPath ($resolvedConfig)"

$standardErrorPath = ""
if ($ProfileLog) {
  $standardErrorPath = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $ProfileLog))
  $env:HEIRAN_PROFILE_PAN = "1"
  # Without this Qt sends the report to OutputDebugString instead of stderr.
  $env:QT_ASSUME_STDERR_HAS_CONSOLE = "1"
  Write-Host "Profile log: $standardErrorPath"
}

if ($NoDisplaySync) {
  $env:HEIRAN_PAN_SYNC = "0"
  Write-Host "Display sync: off"
}

& {
  Invoke-HeiranProcess -Executable $binaryPath -Arguments $arguments -Config $settings `
    -StandardErrorPath $standardErrorPath
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}
