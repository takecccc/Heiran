param(
  [string]$VcpkgRoot,
  [string]$Triplet = "x64-windows",
  [string]$Repository = "https://github.com/microsoft/vcpkg.git",
  [string]$Ref,
  [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$config = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot

$resolvedVcpkgRoot = if ($VcpkgRoot) {
  $VcpkgRoot
} elseif ($config.VcpkgRoot) {
  $config.VcpkgRoot
} else {
  Join-Path $repoRoot ".tools/vcpkg"
}

if (-not [System.IO.Path]::IsPathRooted($resolvedVcpkgRoot)) {
  $resolvedVcpkgRoot = Join-Path $repoRoot $resolvedVcpkgRoot
}

$resolvedVcpkgRoot = [System.IO.Path]::GetFullPath($resolvedVcpkgRoot)
$bootstrapPath = Join-Path $resolvedVcpkgRoot "bootstrap-vcpkg.bat"
$vcpkgExePath = Join-Path $resolvedVcpkgRoot "vcpkg.exe"

Write-Host "Repository root: $repoRoot"
Write-Host "vcpkg root: $resolvedVcpkgRoot"

if (-not (Test-Path $resolvedVcpkgRoot)) {
  Write-Host "Cloning vcpkg from $Repository"
  & git clone $Repository $resolvedVcpkgRoot
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} elseif (-not (Test-Path (Join-Path $resolvedVcpkgRoot ".git"))) {
  throw "The target directory exists but is not a git repository: $resolvedVcpkgRoot"
}

if ($Ref) {
  Write-Host "Checking out vcpkg ref: $Ref"
  & git -C $resolvedVcpkgRoot fetch --tags --force
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  & git -C $resolvedVcpkgRoot checkout $Ref
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if (-not (Test-Path $bootstrapPath)) {
  throw "bootstrap-vcpkg.bat was not found: $bootstrapPath"
}

if (-not (Test-Path $vcpkgExePath)) {
  Write-Host "Bootstrapping vcpkg"
  & $bootstrapPath -disableMetrics
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

if ($SkipInstall) {
  Write-Host "Skipping manifest install."
  return
}

Write-Host "Installing manifest dependencies for triplet: $Triplet"
& $vcpkgExePath install "--x-manifest-root=$repoRoot" "--triplet=$Triplet"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host "Done. The default build uses VCPKG_ROOT or .tools/vcpkg. For a custom location, set:"
Write-Host "  VcpkgRoot = `"$resolvedVcpkgRoot`""
