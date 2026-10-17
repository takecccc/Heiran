function Get-HeiranBuildConfig {
  param(
    [string]$ScriptRoot
  )

  $config = @{
    BuildDir = "build"
    QtPrefixPath = ""
    VcpkgRoot = ""
    CMakeOptions = @()
    BuildConfig = "Debug"
  }

  $localSettingsPath = Join-Path $ScriptRoot "local.settings.ps1"
  if (Test-Path $localSettingsPath) {
    . $localSettingsPath
    if ($HeiranBuildConfig) {
      foreach ($entry in $HeiranBuildConfig.GetEnumerator()) {
        $config[$entry.Key] = $entry.Value
      }
    }
  }

  return $config
}

function Get-HeiranConfiguredBuildType {
  param(
    [string]$BuildDirPath
  )

  $cachePath = Join-Path $BuildDirPath "CMakeCache.txt"
  if (-not (Test-Path $cachePath)) {
    return $null
  }

  $entry = Select-String -Path $cachePath -Pattern "^CMAKE_BUILD_TYPE:STRING=(.*)$" |
    Select-Object -First 1
  if (-not $entry) {
    return $null
  }

  $value = $entry.Matches[0].Groups[1].Value.Trim()
  if (-not $value) {
    return $null
  }

  return $value
}

function Get-HeiranVisualStudioInfo {
  $vswherePath = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswherePath)) {
    return $null
  }

  $installationPath = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  $instanceId = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property instanceId
  if (-not $installationPath -or -not $instanceId) {
    return $null
  }

  return @{
    InstallationPath = $installationPath.Trim()
    InstanceId = $instanceId.Trim()
  }
}

function Get-HeiranVsDevShellScriptPath {
  $vsInfo = Get-HeiranVisualStudioInfo
  if (-not $vsInfo) {
    return $null
  }

  $candidate = Join-Path $vsInfo.InstallationPath "Common7\Tools\Launch-VsDevShell.ps1"
  if (Test-Path $candidate) {
    return $candidate
  }

  return $null
}

function Get-HeiranVsDevShellModulePath {
  $vsInfo = Get-HeiranVisualStudioInfo
  if (-not $vsInfo) {
    return $null
  }

  $candidates = @(
    (Join-Path $vsInfo.InstallationPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"),
    (Join-Path $vsInfo.InstallationPath "Common7\Tools\vsdevshell\Microsoft.VisualStudio.DevShell.dll")
  )

  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  return $null
}

function Enter-HeiranVsDevShell {
  $vsInfo = Get-HeiranVisualStudioInfo
  if (-not $vsInfo) {
    throw "Visual Studio installation was not found. Install Visual Studio C++ tools or run from Developer PowerShell."
  }

  $devShellModulePath = Get-HeiranVsDevShellModulePath
  if (-not $devShellModulePath) {
    throw "Microsoft.VisualStudio.DevShell.dll was not found."
  }

  $launchScriptPath = Get-HeiranVsDevShellScriptPath
  if (-not $launchScriptPath) {
    throw "Launch-VsDevShell.ps1 was not found."
  }

  Import-Module $devShellModulePath -ErrorAction Stop | Out-Null
  $command = Get-Command Enter-VsDevShell -ErrorAction Stop

  $params = @{
    VsInstanceId = $vsInfo.InstanceId
    SkipAutomaticLocation = $true
  }

  if ($command.Parameters.ContainsKey("Arch")) {
    $params.Arch = "amd64"
  }
  if ($command.Parameters.ContainsKey("HostArch")) {
    $params.HostArch = "amd64"
  }

  Write-Host "Using Launch-VsDevShell.ps1: $launchScriptPath"
  Enter-VsDevShell @params
}

function Get-HeiranEnvironmentSnapshot {
  $snapshot = @{}
  foreach ($item in Get-ChildItem Env:) {
    $snapshot[$item.Name] = $item.Value
  }
  return $snapshot
}

function Restore-HeiranEnvironmentSnapshot {
  param(
    [hashtable]$Snapshot
  )

  $currentNames = @((Get-ChildItem Env:).Name)
  foreach ($name in $currentNames) {
    if (-not $Snapshot.ContainsKey($name)) {
      Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
  }

  foreach ($entry in $Snapshot.GetEnumerator()) {
    [System.Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, "Process")
  }
}

function Invoke-HeiranProcess {
  param(
    [string]$Executable,
    [string[]]$Arguments,
    [hashtable]$Config,
    [string]$StandardErrorPath
  )

  $environmentSnapshot = Get-HeiranEnvironmentSnapshot
  $qtBinPath = Join-Path $Config.QtPrefixPath "bin"

  try {
    Enter-HeiranVsDevShell
    if (Test-Path $qtBinPath) {
      $env:PATH = "$qtBinPath;$env:PATH"
    }
    Write-Host "Generator: Ninja"
    Write-Host "Qt toolchain: msvc"

    if (-not $StandardErrorPath) {
      & $Executable @Arguments
      return
    }

    # heiran.exe is a GUI subsystem binary, so no console is attached and
    # PowerShell 2> captures nothing. Hand it an explicit handle instead.
    $startArguments = @{
      FilePath              = $Executable
      RedirectStandardError = $StandardErrorPath
      NoNewWindow           = $true
      Wait                  = $true
      PassThru              = $true
    }
    if ($Arguments -and $Arguments.Count -gt 0) {
      # Start-Process joins the list with spaces and adds no quotes of its own.
      $startArguments.ArgumentList = @($Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
      })
    }
    $process = Start-Process @startArguments
    $global:LASTEXITCODE = $process.ExitCode
  } finally {
    Restore-HeiranEnvironmentSnapshot -Snapshot $environmentSnapshot
  }
}
