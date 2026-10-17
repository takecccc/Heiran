param(
  [string]$BuildDir,
  [string]$QtPrefixPath,
  [switch]$Clean,
  [switch]$Asan
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$config = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot

if ($BuildDir) {
  $config.BuildDir = $BuildDir
}
if ($QtPrefixPath) {
  $config.QtPrefixPath = $QtPrefixPath
}

if (-not $config.QtPrefixPath) {
  throw "QtPrefixPath is not set. Create scripts/local.settings.ps1 from local.settings.sample.ps1."
}

$buildDirPath = Join-Path $repoRoot $config.BuildDir
$cachePath = Join-Path $buildDirPath "CMakeCache.txt"
$cmakeFilesPath = Join-Path $buildDirPath "CMakeFiles"

if ($Clean -and (Test-Path $buildDirPath)) {
  Remove-Item -LiteralPath $cachePath -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force -ErrorAction SilentlyContinue
}

if (Test-Path $cachePath) {
  $cachedCompiler = Select-String -Path $cachePath -Pattern "^CMAKE_CXX_COMPILER:FILEPATH=(.+)$" |
    Select-Object -First 1
  if ($cachedCompiler -and $cachedCompiler.Matches[0].Groups[1].Value -match "clang\+\+") {
    Write-Host "Found an old clang cache in $buildDirPath. Resetting the build directory metadata."
    Remove-Item -LiteralPath $cachePath -Force
    Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force -ErrorAction SilentlyContinue
  }
}

# Ninja is a single-config generator, so the build type must be chosen at configure time.
# The --config passed by build.ps1 is ignored by Ninja.
$resolvedBuildType = if ($Asan) { "RelWithDebInfo" } elseif ($config.BuildConfig) { $config.BuildConfig } else { "Debug" }

$arguments = @(
  "-S", $repoRoot,
  "-B", $buildDirPath,
  "-G", "Ninja",
  "-DCMAKE_BUILD_TYPE=$resolvedBuildType",
  "-DCMAKE_PREFIX_PATH=$($config.QtPrefixPath)"
)

if ($config.VcpkgRoot) {
  $resolvedVcpkgRoot = $config.VcpkgRoot
  if (-not [System.IO.Path]::IsPathRooted($resolvedVcpkgRoot)) {
    $resolvedVcpkgRoot = Join-Path $repoRoot $resolvedVcpkgRoot
  }

  $vcpkgToolchainPath = Join-Path $resolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
  if (-not (Test-Path $vcpkgToolchainPath)) {
    throw "VcpkgRoot is set, but the toolchain file was not found: $vcpkgToolchainPath"
  }

  $hasToolchainOption = $false
  foreach ($option in $config.CMakeOptions) {
    if ($option -like "-DCMAKE_TOOLCHAIN_FILE=*") {
      $hasToolchainOption = $true
      break
    }
  }

  if (-not $hasToolchainOption) {
    $arguments += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchainPath"
  }
}

if ($Asan) {
  $arguments += "-DHEIRAN_ENABLE_ASAN=ON"
}

if ($config.CMakeOptions) {
  $arguments += $config.CMakeOptions
}

Write-Host "Configuring build directory: $buildDirPath"
Write-Host "Qt prefix: $($config.QtPrefixPath)"
Write-Host "Build type: $resolvedBuildType"

& {
  Invoke-HeiranProcess -Executable "cmake" -Arguments $arguments -Config $config
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}
