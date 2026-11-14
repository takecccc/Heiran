param(
  [Parameter(Mandatory = $true)]
  [string]$Version,
  # A separate build directory, so the release build never disturbs the day-to-day one.
  [string]$BuildDir = "build-release",
  [string]$Branch = "main",
  # Delete the whole build directory first (shared vcpkg dependencies are kept).
  [switch]$CleanBuildDir,
  [switch]$SkipTests,
  [switch]$SkipSmokeTest,
  # Second stage. Tags HEAD, pushes, and creates a DRAFT GitHub release from what the first stage prepared.
  # Nothing is published until the draft is published by hand on GitHub.
  [switch]$Publish
)

# Release flow in two stages.
#
#   1. ./scripts/release.ps1 -Version 1.0.0
#        Checks the version, builds a Release package, runs the tests, unpacks the ZIP outside the build tree and starts it,
#        then writes <BuildDir>/release/ (installer, ZIP, SHA256SUMS.txt, release-notes.md).
#        Edit release-notes.md, and try the installer by hand (registry writes are not covered here).
#   2. ./scripts/release.ps1 -Version 1.0.0 -Publish
#        Tags, pushes, and creates a draft release. Needs gh (GitHub CLI), logged in.
#
# Stage 2 refuses to run if HEAD is not the commit stage 1 built.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$tag = "v$Version"
$buildDirPath = Join-Path $repoRoot $BuildDir
$releaseDir = Join-Path $buildDirPath "release"
$installerName = "Heiran-$Version-win64.exe"
$zipName = "Heiran-$Version-win64.zip"

function Invoke-Git {
  param([string[]]$GitArguments)
  $output = & git -C $repoRoot @GitArguments
  if ($LASTEXITCODE -ne 0) {
    throw "git $($GitArguments -join ' ') failed."
  }
  return $output
}

function Invoke-Step {
  param([string]$Title, [scriptblock]$Action)
  Write-Host ""
  Write-Host "=== $Title ==="
  & $Action
  if ($LASTEXITCODE -ne 0) {
    throw "Step failed: $Title (exit code $LASTEXITCODE)"
  }
}

function Write-Utf8File {
  param([string]$Path, [string]$Text)
  [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Read-Utf8File {
  param([string]$Path)
  return [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
}

# ---- Preconditions ----

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
  throw "Version must look like 1.0.0 (got '$Version')."
}

$currentBranch = (Invoke-Git @("rev-parse", "--abbrev-ref", "HEAD") | Select-Object -First 1).Trim()
if ($currentBranch -ne $Branch) {
  throw "Releases are made from '$Branch', but the current branch is '$currentBranch'."
}

$dirty = Invoke-Git @("status", "--porcelain")
if ($dirty) {
  throw "The working tree is not clean. Commit or stash the changes first.`n$($dirty -join "`n")"
}

if (Invoke-Git @("tag", "--list", $tag)) {
  throw "Tag $tag already exists locally."
}

# ---- Version in the files ----

$cmakePath = Join-Path $repoRoot "CMakeLists.txt"
$vcpkgPath = Join-Path $repoRoot "vcpkg.json"
$cmakeText = Read-Utf8File $cmakePath
$vcpkgText = Read-Utf8File $vcpkgPath
$cmakePattern = '(project\(Heiran VERSION )(\d+\.\d+\.\d+)'
$vcpkgPattern = '("version-string":\s*")([^"]+)(")'

if ($cmakeText -notmatch $cmakePattern) {
  throw "The project(Heiran VERSION ...) line was not found in CMakeLists.txt."
}
$cmakeVersion = $Matches[2]
if ($vcpkgText -notmatch $vcpkgPattern) {
  throw "version-string was not found in vcpkg.json."
}
$vcpkgVersion = $Matches[2]

if ($cmakeVersion -ne $Version -or $vcpkgVersion -ne $Version) {
  if ($Publish) {
    throw "CMakeLists.txt ($cmakeVersion) and vcpkg.json ($vcpkgVersion) do not match $Version."
  }
  Write-Utf8File $cmakePath ([regex]::Replace($cmakeText, $cmakePattern, "`${1}$Version"))
  Write-Utf8File $vcpkgPath ([regex]::Replace($vcpkgText, $vcpkgPattern, "`${1}$Version`${3}"))
  Write-Host "Updated the version to $Version in CMakeLists.txt and vcpkg.json."
  Write-Host "Review and commit the change (git diff), then run this script again."
  exit 1
}

# ---- Stage 2: publish ----

if ($Publish) {
  $commitFile = Join-Path $releaseDir "commit.txt"
  $notesPath = Join-Path $releaseDir "release-notes.md"
  $sumsPath = Join-Path $releaseDir "SHA256SUMS.txt"
  $installerPath = Join-Path $releaseDir $installerName
  $zipPath = Join-Path $releaseDir $zipName

  foreach ($required in @($commitFile, $notesPath, $sumsPath, $installerPath, $zipPath)) {
    if (-not (Test-Path $required)) {
      throw "Not found: $required. Run the first stage (without -Publish) first."
    }
  }

  $head = (Invoke-Git @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
  $built = (Read-Utf8File $commitFile).Trim()
  if ($head -ne $built) {
    throw "HEAD ($head) is not the commit the package was built from ($built). Run the first stage again."
  }

  if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "gh (GitHub CLI) is not installed."
  }
  & gh auth status
  if ($LASTEXITCODE -ne 0) {
    throw "gh is not logged in. Run: gh auth login"
  }

  if (Invoke-Git @("ls-remote", "--tags", "origin", "refs/tags/$tag")) {
    throw "Tag $tag already exists on origin."
  }

  Invoke-Step "Push $Branch" { Invoke-Git @("push", "origin", $Branch) | Out-Null }
  Invoke-Step "Tag $tag" {
    Invoke-Git @("tag", "-a", $tag, "-m", "Heiran $Version") | Out-Null
    Invoke-Git @("push", "origin", $tag) | Out-Null
  }
  Invoke-Step "Create the draft release" {
    & gh release create $tag $installerPath $zipPath $sumsPath --draft --title "Heiran $Version" --notes-file $notesPath
  }

  Write-Host ""
  Write-Host "Draft release created. Check it on GitHub and publish it there."
  exit 0
}

# ---- Stage 1: build and verify ----

if (Invoke-Git @("ls-remote", "--tags", "origin", "refs/tags/$tag")) {
  throw "Tag $tag already exists on origin."
}

if ($CleanBuildDir -and (Test-Path $buildDirPath)) {
  Write-Host "Removing $buildDirPath"
  Remove-Item -LiteralPath $buildDirPath -Recurse -Force
}

Invoke-Step "Configure (Release)" {
  & (Join-Path $PSScriptRoot "configure.ps1") -BuildDir $BuildDir -BuildConfig Release -Clean
}
Invoke-Step "Build" {
  & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir
}
if ($SkipTests) {
  Write-Warning "Tests are skipped."
} else {
  Invoke-Step "Test" {
    & (Join-Path $PSScriptRoot "test.ps1") -BuildDir $BuildDir
  }
}

# Old packages would be picked up as the artifacts.
Get-ChildItem -Path $buildDirPath -Filter "Heiran-*" -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Extension -in @(".exe", ".zip") } |
  Remove-Item -Force

# No -Fast: this is the package that gets handed out (LZMA).
Invoke-Step "Package" {
  & (Join-Path $PSScriptRoot "package.ps1") -BuildDir $BuildDir
}

foreach ($name in @($installerName, $zipName)) {
  if (-not (Test-Path (Join-Path $buildDirPath $name))) {
    throw "$name was not created in $buildDirPath."
  }
}

# ---- Smoke test: the ZIP, unpacked outside the build tree ----

if ($SkipSmokeTest) {
  Write-Warning "The smoke test is skipped."
} else {
  Write-Host ""
  Write-Host "=== Smoke test (ZIP) ==="
  $smokeDir = Join-Path ([System.IO.Path]::GetTempPath()) ("heiran-release-" + [System.Guid]::NewGuid().ToString("N"))
  $savedPath = $env:PATH
  try {
    Expand-Archive -LiteralPath (Join-Path $buildDirPath $zipName) -DestinationPath $smokeDir

    $exe = Get-ChildItem -Path $smokeDir -Recurse -Filter "heiran.exe" | Select-Object -First 1
    if (-not $exe -or $exe.Directory.Name -ne "bin") {
      throw "bin/heiran.exe was not found in the ZIP."
    }
    $packageRoot = $exe.Directory.Parent.FullName
    foreach ($expected in @("LICENSE.txt", "NOTICE", "THIRD_PARTY_NOTICES.txt", "README.md", "doc\manual\index.html")) {
      if (-not (Test-Path (Join-Path $packageRoot $expected))) {
        throw "$expected is missing from the ZIP."
      }
    }

    # Qt and vcpkg DLLs on PATH would hide DLLs missing from the package.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = Start-Process -FilePath $exe.FullName -WorkingDirectory $smokeDir -PassThru
    Start-Sleep -Seconds 8
    if ($process.HasExited) {
      throw "heiran.exe exited within 8 seconds (exit code $($process.ExitCode))."
    }
    Write-Host "heiran.exe is running. Closing it."
    $null = $process.CloseMainWindow()
    if (-not $process.WaitForExit(5000)) {
      $process.Kill()
    }
  } finally {
    $env:PATH = $savedPath
    if (Test-Path $smokeDir) {
      Remove-Item -LiteralPath $smokeDir -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  Write-Host "Smoke test passed."
}

# ---- Release directory ----

if (Test-Path $releaseDir) {
  Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseDir | Out-Null
foreach ($name in @($installerName, $zipName)) {
  Copy-Item -LiteralPath (Join-Path $buildDirPath $name) -Destination $releaseDir
}

$hashes = @{}
$sumLines = @()
foreach ($name in @($installerName, $zipName)) {
  $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $releaseDir $name)).Hash.ToLowerInvariant()
  $hashes[$name] = $hash
  $sumLines += "$hash  $name"
}
Write-Utf8File (Join-Path $releaseDir "SHA256SUMS.txt") (($sumLines -join "`n") + "`n")

$head = (Invoke-Git @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
Write-Utf8File (Join-Path $releaseDir "commit.txt") ($head + "`n")

# Release notes draft. git prints UTF-8; read it as such.
$previousTag = $null
$savedErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$previousTag = & git -C $repoRoot describe --tags --abbrev=0 --match "v*" 2>$null
$ErrorActionPreference = $savedErrorAction
$changes = "(Initial release. Replace this line.)"
if ($LASTEXITCODE -eq 0 -and $previousTag) {
  $savedEncoding = [Console]::OutputEncoding
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  try {
    $changes = (Invoke-Git @("log", "$previousTag..HEAD", "--pretty=format:- %s")) -join "`n"
  } finally {
    [Console]::OutputEncoding = $savedEncoding
  }
}

$template = Read-Utf8File (Join-Path $repoRoot "resources\release\release-notes.template.md")
$notes = $template.Replace("{{VERSION}}", $Version).
  Replace("{{SHA256_INSTALLER}}", $hashes[$installerName]).
  Replace("{{SHA256_ZIP}}", $hashes[$zipName]).
  Replace("{{INSTALLER_NAME}}", $installerName).
  Replace("{{ZIP_NAME}}", $zipName).
  Replace("{{CHANGES}}", $changes)
Write-Utf8File (Join-Path $releaseDir "release-notes.md") $notes

Write-Host ""
Write-Host "Prepared: $releaseDir"
Get-ChildItem -Path $releaseDir | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host ""
Write-Host "Next:"
Write-Host "  1. Edit release-notes.md."
Write-Host "  2. Install the installer on a clean machine (or VM) and check it by hand."
Write-Host "  3. ./scripts/release.ps1 -Version $Version -Publish   (creates a draft release)"
