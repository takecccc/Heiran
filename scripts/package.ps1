param(
  [string]$BuildDir,
  [string]$Generator,
  # Compress the installer with zlib instead of LZMA. About twice as fast (about 25 s instead of 50 s), and about 20 percent
  # bigger. For trying a package while developing. Build the package to hand out without it.
  [switch]$Fast
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$settings = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot
$resolvedBuildDir = $settings.BuildDir

if ($BuildDir) {
  $resolvedBuildDir = $BuildDir
}

$buildDirPath = Join-Path $repoRoot $resolvedBuildDir

$configuredBuildType = Get-HeiranConfiguredBuildType -BuildDirPath $buildDirPath
if (-not $configuredBuildType) {
  throw "$buildDirPath is not configured. Run configure.ps1 first."
}
if ($configuredBuildType -ne "Release") {
  Write-Warning "Packaging a $configuredBuildType build. Run configure.ps1 with -Config Release for a build to hand out."
}

# Package what is currently built. Build first so the package is not made from stale binaries.
& (Join-Path $PSScriptRoot "build.ps1") -BuildDir $resolvedBuildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

# The installer and the ZIP carry the user manual (Help > Manual opens it).
& (Join-Path $PSScriptRoot "build-manual.ps1") -BuildDir $resolvedBuildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

# cpack takes the build configuration with -C. --config names a CPack config file.
$arguments = @("-C", $configuredBuildType)
if ($Generator) {
  $arguments += @("-G", $Generator)
}
if ($Fast) {
  $arguments += @("-D", "CPACK_NSIS_COMPRESSOR=zlib")
}

Write-Host "Packaging: $buildDirPath ($configuredBuildType)"

Push-Location $buildDirPath
try {
  & {
    Invoke-HeiranProcess -Executable "cpack" -Arguments $arguments -Config $settings
    if ($LASTEXITCODE -ne 0) {
      exit $LASTEXITCODE
    }
  }
} finally {
  Pop-Location
}

Get-ChildItem -Path $buildDirPath -Filter "Heiran-*" -File |
  Where-Object { $_.Extension -in @(".exe", ".zip") } |
  ForEach-Object { Write-Host "Created: $($_.FullName)" }
