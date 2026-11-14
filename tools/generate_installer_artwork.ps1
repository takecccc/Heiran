param(
  [string]$OutputDir = (Join-Path $PSScriptRoot "../resources/nsis")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$scale = 3
$outputPath = [System.IO.Path]::GetFullPath($OutputDir)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

function New-Artwork {
  param([int]$Width, [int]$Height)

  $bitmap = New-Object System.Drawing.Bitmap ($Width * $scale), ($Height * $scale), ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $bitmap.SetResolution(96, 96)
  return $bitmap
}

function New-ArtworkGraphics {
  param([System.Drawing.Bitmap]$Bitmap)

  $graphics = [System.Drawing.Graphics]::FromImage($Bitmap)
  $graphics.ScaleTransform($scale, $scale)
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  return $graphics
}

$iconPath = Join-Path $PSScriptRoot "../resources/heiran-256.png"
$icon = [System.Drawing.Image]::FromFile([System.IO.Path]::GetFullPath($iconPath))
$sidebar = New-Artwork -Width 164 -Height 314
$header = New-Artwork -Width 150 -Height 57
$sidebarGraphics = New-ArtworkGraphics -Bitmap $sidebar
$headerGraphics = New-ArtworkGraphics -Bitmap $header
$gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.Rectangle]::new(0, 0, 164, 314)), ([System.Drawing.ColorTranslator]::FromHtml("#343E4D")), ([System.Drawing.ColorTranslator]::FromHtml("#20262F")), 90
$accent = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#FFC24A"))
$ink = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml("#F0F3F6"))
$line = New-Object System.Drawing.Pen ([System.Drawing.ColorTranslator]::FromHtml("#414B59")), 1
$font = New-Object System.Drawing.Font "Segoe UI", 25, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
try {
  $sidebarGraphics.FillRectangle($gradient, 0, 0, 164, 314)
  $sidebarGraphics.FillRectangle($accent, 24, 30, 24, 3)
  $sidebarGraphics.DrawString("Heiran", $font, $ink, 21, 44)
  $sidebarGraphics.DrawImage($icon, 30, 118, 104, 104)
  $sidebarGraphics.DrawLine($line, 24, 279, 140, 279)
  $sidebarGraphics.FillRectangle($accent, 24, 279, 24, 1)
  $headerGraphics.Clear([System.Drawing.Color]::White)
  $headerGraphics.DrawImage($icon, 98, 8, 40, 40)
  $sidebar.Save((Join-Path $outputPath "sidebar.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
  $header.Save((Join-Path $outputPath "header.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
} finally {
  foreach ($resource in @($font, $line, $ink, $accent, $gradient, $sidebarGraphics, $headerGraphics, $sidebar, $header, $icon)) {
    $resource.Dispose()
  }
}

Write-Host "Created installer artwork in $outputPath"
