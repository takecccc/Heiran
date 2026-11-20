param(
  [string]$BuildDir,
  # Capture only the scenes whose name contains this text.
  [string]$Only
)

# Captures the screenshots used by the user manual (manual/_static/images) with heiran_screenshot.
# The sample pictures come from examples/images. A temporary drive letter (subst) hides the
# repository path, so the file names in the screenshots stay neutral.
# The screenshots show the Japanese screen (--lang ja), so the manual names every menu in Japanese with the English name beside it.
# Run it again after the look of the application changes. Review the images before committing them.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "common.ps1")
$settings = Get-HeiranBuildConfig -ScriptRoot $PSScriptRoot
$resolvedBuildDir = $settings.BuildDir
if ($BuildDir) {
  $resolvedBuildDir = $BuildDir
}

$executable = Join-Path $repoRoot "$resolvedBuildDir/heiran_screenshot.exe"
if (-not (Test-Path $executable)) {
  throw "Executable was not found: $executable. Build first."
}

$outputDirectory = Join-Path $repoRoot "manual/_static/images"
New-Item -ItemType Directory -Force $outputDirectory | Out-Null

# Pick a free drive letter for the sample pictures.
$used = (Get-PSDrive -PSProvider FileSystem).Name
$letter = ([char[]]"ZYXWVUTSRQPONM" | Where-Object { $used -notcontains [string]$_ } | Select-Object -First 1)
if (-not $letter) {
  throw "No free drive letter."
}
$drive = "${letter}:"
subst $drive (Join-Path $repoRoot "examples/images")
$animationLetter = ([char[]]"ZYXWVUTSRQPONM" | Where-Object { $used -notcontains [string]$_ -and "${_}:" -ne $drive } | Select-Object -First 1)
$animationDrive = "${animationLetter}:"
subst $animationDrive (Join-Path $repoRoot "examples/animation")

try {
  $a = "$drive/a/0001.png"
  $b = "$drive/b/0001.png"
  $c = "$drive/c/0001.png"
  $d = "$drive/d/0001.png"
  $gif = "$animationDrive/bounce-optimized.gif"
  $favoritesPath = Join-Path $repoRoot "$resolvedBuildDir/manual-favorites.json"
  @{ version = 1; files = @($a, "$drive/a/0003.png") } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $favoritesPath -Encoding Ascii

  # name, arguments
  $scenes = @(
    @("overview", @("--size", "1100x640", $a, $b)),
    @("welcome-small", @("--size", "440x560", "--recent-image", $a, "--recent-image", $b, "--recent-image", $c, "--recent-image", $d, "--recent-image", "$drive/a/0002.png")),
    @("compact-navigation", @("--size", "640x480", "--action", "Cell Headers", "--action", "Navigation", $a, $b)),
    @("pixel-info", @("--size", "1100x640", "--action", "Pixel Info", "--cursor", "0,150,110", $a, $b)),
    @("solo-cell", @("--size", "1100x640", "--solo", "1", $a, $b)),
    @("layout-2x2", @("--size", "1100x700", $a, $b, $c, $d)),
    @("menu-layout", @("--size", "1100x640", "--menu", "Layout", $a, $b)),
    @("menu-view", @("--size", "1100x640", "--menu", "View", $a, $b)),
    @("diff-cell", @("--size", "1300x640", "--diff-cell", "--hover-cell", "2", $a, $b)),
    @("diff-delta-e", @("--size", "1300x640", "--hrn", "DRIVE_SESSION", "--action", "Fit", "--hover-cell", "2")),
    @("diff-settings", @("--size", "1300x640", "--hrn", "DRIVE_SESSION", "--action", "Fit", "--hover-cell", "2", "--diff-popup")),
    @("pages", @("--size", "1100x640", "--favorites-file", $favoritesPath, "--action", "Image List", $a, $b)),
    @("image-list-large-thumbnails", @("--size", "1200x700", "--thumbnail-width", "256", "--favorites-file", $favoritesPath, "--action", "Image List", $a)),
    @("zoom-percent", @("--size", "1100x640", "--zoom-percent", "150.25", $a, $b)),
    @("image-list-details", @("--size", "1200x700", "--list-mode", "details", "--favorites-file", $favoritesPath, "--action", "Image List", "--action", "Favorites Only", $a)),
    @("image-list-selection", @("--size", "1200x700", "--list-mode", "details", "--favorites-file", $favoritesPath, "--action", "Image List", "--list-select", "0,2", $a)),
    @("image-list-comparison", @("--size", "1200x700", "--list-mode", "details", "--favorites-file", $favoritesPath, "--action", "Image List", "--list-select", "0,2", "--compare-selection", $a)),
    @("image-list-filtered-comparison", @("--size", "1200x700", "--list-mode", "details", "--favorites-file", $favoritesPath, "--action", "Image List", "--list-select", "0,1", "--compare-selection", "--favorites-only-after-comparison", $a)),
    @("image-list-context-menu", @("--size", "1200x700", "--list-mode", "details", "--favorites-file", $favoritesPath, "--action", "Image List", "--list-select", "0,2", "--list-context-menu", $a)),
    @("image-sets", @("--size", "1100x700", "--dialog", "Manage Image Sets...", $a, $b, $c, $d)),
    @("image-set-cells", @("--size", "1100x700", "--dialog", "Manage Image Sets...", "--dialog-tab", "Cells", $a, $b, $c, $d)),
    @("animation", @("--size", "900x560", $gif)),
    @("diff-ssim", @("--size", "1300x640", "--hrn", "DRIVE_SSIM_SESSION", "--action", "Fit", "--hover-cell", "2")),
    @("wipe", @("--size", "1300x640", "--hrn", "DRIVE_WIPE_SESSION", "--action", "Fit", "--hover-cell", "2")),
    @("wipe-settings", @("--size", "1300x640", "--hrn", "DRIVE_WIPE_SESSION", "--action", "Fit", "--hover-cell", "2", "--diff-popup")),
    @("settings", @("--dialog", "Settings...", "--dialog-category", "Display and playback", "--dialog-focus", "magnifyFilter", $a)),
    @("transparency-background-menu", @("--context-cell", "0", "--menu", "Transparency background", $a)),
    @("histogram", @("--size", "1100x640", "--analysis", "Histogram", $a, $b)),
    @("waveform", @("--size", "1100x640", "--analysis", "Waveform", $a, $b)),
    @("vectorscope", @("--size", "1100x640", "--analysis", "Vectorscope", $a, $b)),
    @("change-graph", @("--size", "1100x640", "--analysis", "Change Graph", $a, $b)),
    @("image-info", @("--size", "1100x640", "--analysis", "Image Info...", $a, $b))
  )

  foreach ($scene in $scenes) {
    $name = $scene[0]
    if ($Only -and ($name -notlike "*$Only*")) {
      continue
    }
    $path = Join-Path $outputDirectory "$name.png"
    Write-Host "Capturing: $name"
    # The session scene needs the sample pictures under the temporary drive letter.
    $sceneArguments = $scene[1]
    foreach ($marker in @(@("DRIVE_SESSION", "diff-delta-e.hrn"), @("DRIVE_WIPE_SESSION", "wipe.hrn"), @("DRIVE_SSIM_SESSION", "diff-ssim.hrn"))) {
      if ($sceneArguments -contains $marker[0]) {
        $sessionPath = Join-Path ([System.IO.Path]::GetTempPath()) "heiran-manual-session.hrn"
        $sessionText = (Get-Content (Join-Path $repoRoot "examples/$($marker[1])") -Raw) -replace "images/", "$drive/"
        Set-Content -Path $sessionPath -Value $sessionText -Encoding UTF8
        $replacement = $marker[0]
        $sceneArguments = $sceneArguments | ForEach-Object { if ($_ -eq $replacement) { $sessionPath } else { $_ } }
      }
    }
    $arguments = @($path, "--theme", "dark", "--lang", "ja", "--transparency", "solid") + $sceneArguments
    # A non-empty StandardErrorPath makes the call wait for the GUI process to end. Without it the
    # next scene starts at once, and the temporary drive is removed while the tool still reads it.
    $errorPath = Join-Path ([System.IO.Path]::GetTempPath()) "heiran-screenshot.err"
    Invoke-HeiranProcess -Executable $executable -Arguments $arguments -Config $settings -StandardErrorPath $errorPath
    if ($LASTEXITCODE -ne 0) {
      throw "heiran_screenshot failed for $name (exit code $LASTEXITCODE)."
    }
  }
} finally {
  subst $drive /d
  subst $animationDrive /d
}

Write-Host "Done: $outputDirectory"
