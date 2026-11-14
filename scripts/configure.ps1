param(
  [string]$BuildDir,
  [string]$QtPrefixPath,
  # Overrides the shared default or local.settings.ps1.
  [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$BuildConfig,
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
if ($BuildConfig) {
  $config.BuildConfig = $BuildConfig
}

if (-not $config.QtPrefixPath) {
  throw "Qt was not configured. Set Qt6_ROOT to your Qt MSVC installation prefix (for example C:/Qt/6.11.1/msvc2022_64), or use -QtPrefixPath."
}

$resolvedVcpkgRoot = $config.VcpkgRoot
if (-not $resolvedVcpkgRoot) {
  throw "VcpkgRoot must not be empty. Use VCPKG_ROOT or run scripts/setup-vcpkg.ps1."
}
if (-not [System.IO.Path]::IsPathRooted($resolvedVcpkgRoot)) {
  $resolvedVcpkgRoot = Join-Path $repoRoot $resolvedVcpkgRoot
}
$vcpkgToolchainPath = Join-Path $resolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path $vcpkgToolchainPath)) {
  throw "vcpkg toolchain not found: $vcpkgToolchainPath. Run scripts/setup-vcpkg.ps1."
}

foreach ($option in $config.CMakeOptions) {
  if ($option -match '^-D\s*(CMAKE_CXX_COMPILER|CMAKE_TOOLCHAIN_FILE|VCPKG_MANIFEST_MODE|VCPKG_MANIFEST_INSTALL|VCPKG_MANIFEST_DIR|VCPKG_INSTALLED_DIR)(:[^=]+)?=') {
    throw "The build scripts manage this option; remove it from CMakeOptions: $option"
  }
}

$clangClPath = Get-HeiranClangClPath
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $config.BuildDir))
if ($Clean -and -not $buildDirPath.StartsWith("$repoRoot\", [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "-Clean requires a build directory inside the repository: $buildDirPath"
}
$cachePath = Join-Path $buildDirPath "CMakeCache.txt"
$cmakeFilesPath = Join-Path $buildDirPath "CMakeFiles"

if ($Clean -and (Test-Path $buildDirPath)) {
  Remove-Item -LiteralPath $cachePath -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $cmakeFilesPath -Recurse -Force -ErrorAction SilentlyContinue
}

if (Test-Path $cachePath) {
  $cachedCompiler = Select-String -Path $cachePath -Pattern "^CMAKE_CXX_COMPILER:(FILEPATH|STRING)=(.+)$" |
    Select-Object -First 1
  if ($cachedCompiler -and
      [System.IO.Path]::GetFullPath($cachedCompiler.Matches[0].Groups[2].Value) -ine
      [System.IO.Path]::GetFullPath($clangClPath)) {
    throw "The compiler in $buildDirPath differs from clang-cl. Run configure.ps1 -Clean with the same -BuildDir to switch compilers."
  }
}

# Ninja is a single-config generator, so the build type must be chosen at configure time.
# The --config passed by build.ps1 is ignored by Ninja.
$resolvedBuildType = if ($Asan) { "RelWithDebInfo" } else { $config.BuildConfig }

$arguments = @(
  "-S", $repoRoot,
  "-B", $buildDirPath,
  "-G", "Ninja",
  "-DCMAKE_CXX_COMPILER=$clangClPath",
  "-DCMAKE_BUILD_TYPE=$resolvedBuildType",
  "-DQt6_ROOT=$($config.QtPrefixPath)",
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchainPath",
  "-DVCPKG_MANIFEST_MODE=ON",
  "-DVCPKG_MANIFEST_INSTALL=ON",
  "-DVCPKG_MANIFEST_DIR=$repoRoot",
  "-DVCPKG_INSTALLED_DIR=$repoRoot/vcpkg_installed"
)

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
