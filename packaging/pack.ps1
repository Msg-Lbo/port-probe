param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("x86","x64")]
  [string]$Arch,

  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  [Parameter(Mandatory = $true)]
  [string]$QtBinDir,

  [Parameter(Mandatory = $false)]
  [ValidateSet("msvc","mingw")]
  [string]$Toolchain = "msvc"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$distRoot = Join-Path $repoRoot "dist"
$distDir = Join-Path $distRoot $Arch

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Get-ChildItem -Path $distDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

$exe = Join-Path $BuildDir "Release/PortProbeQt.exe"
if (!(Test-Path $exe)) {
  # fallback for different generators
  $exe = Join-Path $BuildDir "PortProbeQt.exe"
}
if (!(Test-Path $exe)) {
  throw "PortProbeQt.exe not found under build dir: $BuildDir"
}

Copy-Item $exe (Join-Path $distDir "PortProbeQt.exe") -Force

$windeployqt = Join-Path $QtBinDir "windeployqt.exe"
if (!(Test-Path $windeployqt)) {
  throw "windeployqt.exe not found: $windeployqt"
}

Push-Location $distDir
try {
  if ($Toolchain -eq "msvc") {
    & $windeployqt ".\PortProbeQt.exe" --release --compiler-runtime --no-translations
  } else {
    & $windeployqt ".\PortProbeQt.exe" --release --no-translations
  }
} finally {
  Pop-Location
}

if ($Toolchain -eq "msvc") {
  # On some machines windeployqt cannot locate VC runtime automatically.
  # Copy common VC14 runtime DLLs manually as a fallback.
  $vcRuntimeNames = @(
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "concrt140.dll"
  )

  $searchRoots = @()
  if ($env:VCINSTALLDIR) { $searchRoots += $env:VCINSTALLDIR }
  $searchRoots += @(
    "C:\Windows\System32",
    "C:\Windows\SysWOW64",
    "C:\Program Files (x86)\Microsoft Visual Studio",
    "C:\Program Files\Microsoft Visual Studio"
  )

  foreach ($dll in $vcRuntimeNames) {
    $target = Join-Path $distDir $dll
    if (Test-Path $target) { continue }

    $copied = $false
    foreach ($root in $searchRoots) {
      if (!(Test-Path $root)) { continue }
      $match = Get-ChildItem -Path $root -Filter $dll -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
      if ($match) {
        Copy-Item $match.FullName $target -Force
        $copied = $true
        break
      }
    }

    if (-not $copied) {
      Write-Warning "Could not locate VC runtime: $dll"
    }
  }
}
else {
  # MinGW stable mode: copy only required Qt runtime DLLs to keep package size reasonable.
  $qtDllNames = @(
    "Qt5Core.dll",
    "Qt5Gui.dll",
    "Qt5Widgets.dll",
    "Qt5Network.dll",
    "Qt5Svg.dll",
    "libEGL.dll",
    "libGLESv2.dll",
    "opengl32sw.dll",
    "D3Dcompiler_47.dll"
  )
  foreach ($dll in $qtDllNames) {
    $src = Join-Path $QtBinDir $dll
    if (Test-Path $src) {
      Copy-Item $src (Join-Path $distDir $dll) -Force
    }
  }

  $qtRoot = Split-Path -Parent $QtBinDir
  $pluginRoot = Join-Path $qtRoot "plugins"
  $pluginDirs = @("platforms", "styles", "imageformats", "iconengines", "bearer")
  foreach ($pd in $pluginDirs) {
    $src = Join-Path $pluginRoot $pd
    if (Test-Path $src) {
      $dst = Join-Path $distDir $pd
      New-Item -ItemType Directory -Force -Path $dst | Out-Null
      # Copy only files to reduce unnecessary payload.
      Copy-Item (Join-Path $src "*.dll") $dst -Force -ErrorAction SilentlyContinue
    }
  }

  $mingwCandidates = @(
    "D:\Qt\Tools\mingw810_64\bin",
    "D:\Qt\Tools\mingw810_32\bin",
    "D:\Qt\Tools\mingw1310_64\bin"
  )
  $mingwRuntimeNames = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "libgcc_s_dw2-1.dll")
  foreach ($dll in $mingwRuntimeNames) {
    $target = Join-Path $distDir $dll
    if (Test-Path $target) { continue }
    foreach ($root in $mingwCandidates) {
      $src = Join-Path $root $dll
      if (Test-Path $src) {
        Copy-Item $src $target -Force
        break
      }
    }
  }
}

$required = @("PortProbeQt.exe", "platforms/qwindows.dll")
if ($Toolchain -eq "msvc") {
  $required += @("msvcp140.dll", "vcruntime140.dll")
} else {
  $required += @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
}
foreach ($r in $required) {
  $p = Join-Path $distDir $r
  if (!(Test-Path $p)) {
    throw "Missing required runtime file after deploy: $p"
  }
}

Write-Host "Packed to $distDir"

# Also generate a single-file distributable archive.
$bundleName = "PortProbeQt_${Arch}_all_in_one.zip"
$bundlePath = Join-Path $distRoot $bundleName
if (Test-Path $bundlePath) {
  Remove-Item $bundlePath -Force
}
Compress-Archive -Path (Join-Path $distDir "*") -DestinationPath $bundlePath -CompressionLevel Optimal
Write-Host "Single-file bundle created: $bundlePath"

