param(
  [string]$BuildDir,
  [string]$Config,
  [switch]$ConfigureFirst
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

if ($ConfigureFirst) {
  & (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $resolvedBuildDir
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
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
$arguments = @("--build", $buildDirPath, "--config", $resolvedConfig)

Write-Host "Building: $buildDirPath ($resolvedConfig)"

& {
  Invoke-HeiranProcess -Executable "cmake" -Arguments $arguments -Config $settings
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}
