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
$appBaseName = [string]::Concat([char[]](0x63A2, 0x6D4B, 0x5DE5, 0x5177))
$appExeName = "$appBaseName.exe"

$mingwCompilerBin = $null
$cmakeCache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cmakeCache) {
  $compilerLine = Get-Content $cmakeCache | Where-Object { $_ -like "CMAKE_CXX_COMPILER:*" } | Select-Object -First 1
  if ($compilerLine -and ($compilerLine -match "=(.+)$")) {
    $compilerPath = $Matches[1].Trim()
    if (Test-Path $compilerPath) {
      $mingwCompilerBin = Split-Path -Parent $compilerPath
    }
  }
}

if ($Toolchain -eq "mingw") {
  # GCC helper executables and Qt DLLs must be discoverable when CMake/windeployqt
  # launch child processes. This is required on clean machines and in CI.
  $pathParts = @($QtBinDir)
  if ($mingwCompilerBin) {
    $pathParts += $mingwCompilerBin
  }
  $env:PATH = (($pathParts | Where-Object { $_ -and (Test-Path $_) }) -join ";") + ";" + $env:PATH
}

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Get-ChildItem -Path $distDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

$exe = Join-Path $BuildDir "Release/$appExeName"
if (!(Test-Path $exe)) {
  # fallback for different generators
  $exe = Join-Path $BuildDir $appExeName
}
if (!(Test-Path $exe)) {
  $exe = Join-Path $BuildDir "Release/PortProbeQt.exe"
}
if (!(Test-Path $exe)) {
  $exe = Join-Path $BuildDir "PortProbeQt.exe"
}
if (!(Test-Path $exe)) {
  throw "$appExeName not found under build dir: $BuildDir"
}

Copy-Item $exe (Join-Path $distDir $appExeName) -Force

$windeployqt = Join-Path $QtBinDir "windeployqt.exe"
if (!(Test-Path $windeployqt)) {
  throw "windeployqt.exe not found: $windeployqt"
}

Push-Location $distDir
try {
  if ($Toolchain -eq "msvc") {
    & $windeployqt ".\$appExeName" --release --compiler-runtime --no-translations
  } else {
    & $windeployqt ".\$appExeName" --release --no-translations
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

  $mingwCandidates = @()

  if ($mingwCompilerBin) {
    $mingwCandidates += $mingwCompilerBin
  }

  # Prefer environment-provided MinGW bin path in CI.
  if ($env:GXX_EXE) {
    $gxxDir = Split-Path -Parent $env:GXX_EXE
    if ($gxxDir) {
      $mingwCandidates += $gxxDir
    }
  }

  # Derive Qt Tools directory from Qt bin path (works for both local and CI layouts).
  $qtRoot = Split-Path -Parent $QtBinDir
  $qtVersionRoot = Split-Path -Parent $qtRoot
  $qtBaseRoot = Split-Path -Parent $qtVersionRoot
  $qtToolsRoot = Join-Path $qtBaseRoot "Tools"
  if (Test-Path $qtToolsRoot) {
    $toolBins = Get-ChildItem -Path $qtToolsRoot -Recurse -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -eq "bin" } |
      Select-Object -ExpandProperty FullName
    if ($Arch -eq "x64") {
      $toolBins = $toolBins | Where-Object { $_ -match "mingw.*64|x86_64" }
    } else {
      $toolBins = $toolBins | Where-Object { $_ -match "mingw.*32|i686" }
    }
    if ($toolBins) {
      $mingwCandidates += $toolBins
    }
  }

  # Backward-compatible local fallback paths.
  if ($Arch -eq "x64") {
    $mingwCandidates += @(
      "D:\Qt\Tools\mingw810_64\bin",
      "D:\Qt\Tools\mingw1310_64\bin"
    )
  } else {
    $mingwCandidates += @(
      "D:\Qt\Tools\mingw810_32\bin"
    )
  }

  $mingwCandidates = $mingwCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique
  if ($Arch -eq "x64") {
    $mingwRuntimeNames = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
    $opensslRuntimeNames = @("libcrypto-1_1-x64.dll", "libssl-1_1-x64.dll")
  } else {
    $mingwRuntimeNames = @("libgcc_s_dw2-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
    $opensslRuntimeNames = @("libcrypto-1_1.dll", "libssl-1_1.dll")
  }
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

  # Qt 5 MinGW loads OpenSSL at runtime for HTTPS requests such as update checks.
  # The OpenSSL DLLs shipped with the matching Qt MinGW toolchain live under opt\bin.
  $opensslCandidates = @()
  foreach ($root in $mingwCandidates) {
    $opensslCandidates += $root

    $toolRoot = Split-Path -Parent $root
    if ($toolRoot) {
      $opensslCandidates += (Join-Path $toolRoot "opt\bin")

      $optRoot = Split-Path -Parent $toolRoot
      if ($optRoot) {
        $opensslCandidates += (Join-Path $optRoot "opt\bin")
      }
    }
  }

  if ($Arch -eq "x64") {
    $opensslCandidates += @(
      "D:\Qt\Tools\mingw810_64\opt\bin",
      "D:\Qt\Tools\mingw1310_64\opt\bin",
      "C:\Qt\Tools\mingw810_64\opt\bin",
      "C:\Qt\Tools\mingw1310_64\opt\bin",
      "C:\Program Files\OpenSSL-Win64\bin",
      "C:\OpenSSL-Win64\bin"
    )
  } else {
    $opensslCandidates += @(
      "D:\Qt\Tools\mingw810_32\opt\bin",
      "C:\Qt\Tools\mingw810_32\opt\bin",
      "C:\Program Files (x86)\OpenSSL-Win32\bin",
      "C:\OpenSSL-Win32\bin"
    )
  }

  $opensslCandidates = $opensslCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique
  foreach ($dll in $opensslRuntimeNames) {
    $target = Join-Path $distDir $dll
    if (Test-Path $target) { continue }
    foreach ($root in $opensslCandidates) {
      $src = Join-Path $root $dll
      if (Test-Path $src) {
        Copy-Item $src $target -Force
        break
      }
    }
  }

  # qdirect2d depends on Direct2D platform updates that are not present on Win7 RTM.
  # Keep qwindows.dll only so the package has no SP1/Platform Update requirement.
  $direct2dPlugin = Join-Path $distDir "platforms/qdirect2d.dll"
  if (Test-Path $direct2dPlugin) {
    Remove-Item $direct2dPlugin -Force
  }
}

$required = @($appExeName, "platforms/qwindows.dll")
if ($Toolchain -eq "msvc") {
  $required += @("msvcp140.dll", "vcruntime140.dll")
} else {
  if ($Arch -eq "x64") {
    $required += @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
  } else {
    $required += @("libgcc_s_dw2-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
  }
  if ($Arch -eq "x64") {
    $required += @("libcrypto-1_1-x64.dll", "libssl-1_1-x64.dll")
  } else {
    $required += @("libcrypto-1_1.dll", "libssl-1_1.dll")
  }
}
foreach ($r in $required) {
  $p = Join-Path $distDir $r
  if (!(Test-Path $p)) {
    throw "Missing required runtime file after deploy: $p"
  }
}

Write-Host "Packed to $distDir"

# Also generate a single-file distributable archive.
$bundleName = "ProbeTool_${Arch}_all_in_one.zip"
$bundlePath = Join-Path $distRoot $bundleName
if (Test-Path $bundlePath) {
  Remove-Item $bundlePath -Force
}
Compress-Archive -Path (Join-Path $distDir "*") -DestinationPath $bundlePath -CompressionLevel Optimal
Write-Host "Single-file bundle created: $bundlePath"
