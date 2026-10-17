param(
  [string]$BuildDir,
  [string]$Config
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

$buildDirPath = Join-Path $repoRoot $resolvedBuildDir

# The configured build type wins. Re-run configure.ps1 when it differs from the settings.
$configuredBuildType = Get-HeiranConfiguredBuildType -BuildDirPath $buildDirPath
if ($configuredBuildType) {
  if ($Config -and $Config -ne $configuredBuildType) {
    Write-Warning "Requested $Config but $buildDirPath is configured as $configuredBuildType. Run configure.ps1 first."
  }
  $resolvedConfig = $configuredBuildType
}
$arguments = @("--test-dir", $buildDirPath, "--build-config", $resolvedConfig, "--output-on-failure")

Write-Host "Running tests in: $buildDirPath ($resolvedConfig)"

& {
  Invoke-HeiranProcess -Executable "ctest" -Arguments $arguments -Config $settings
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}
