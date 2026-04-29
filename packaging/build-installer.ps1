param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("x86","x64")]
  [string]$Arch,

  [Parameter(Mandatory = $false)]
  [string]$Version = "1.0.0",

  [Parameter(Mandatory = $false)]
  [string]$IsccPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$distDir = Join-Path $repoRoot "dist/$Arch"
if (!(Test-Path $distDir)) {
  throw "dist directory not found: $distDir. Run packaging/pack.ps1 first."
}

$iscc = $null
if ($IsccPath -and (Test-Path $IsccPath)) {
  $iscc = $IsccPath
}
if (-not $iscc) {
  $isccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
  )
  $iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) {
  throw "ISCC.exe not found. Please install Inno Setup, or pass -IsccPath."
}

$issPath = Join-Path $PSScriptRoot "installer.iss"

Push-Location $repoRoot
try {
  & $iscc "/DMyArch=$Arch" "/DMyAppVersion=$Version" "$issPath"
} finally {
  Pop-Location
}

Write-Host "Installer build done. Check installer_output directory."

