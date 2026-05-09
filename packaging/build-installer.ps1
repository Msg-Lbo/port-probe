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
    "D:\Inno Setup 5\ISCC.exe",
    "C:\Inno Setup 5\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe",
    "C:\Program Files\Inno Setup 5\ISCC.exe"
  )
  $iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) {
  throw "Inno Setup 5 ISCC.exe not found. Install Inno Setup 5.6.1, or pass -IsccPath."
}

$isccVersion = (Get-Item $iscc).VersionInfo
$isccResolvedPath = (Resolve-Path $iscc).Path
if ($isccVersion.FileMajorPart -ge 6 -or $isccResolvedPath -match "Inno Setup 6") {
  throw "Win7 RTM installer must be built with Inno Setup 5.6.1. Inno Setup 6 generated setup launchers can require Windows 7 SP1."
}
if ($isccResolvedPath -notmatch "Inno Setup 5" -and $isccVersion.ProductName -notmatch "Inno Setup") {
  throw "Unsupported ISCC.exe. Expected Inno Setup 5.6.1: $iscc"
}

$issPath = Join-Path $PSScriptRoot "installer.iss"
$appName = [string]::Concat([char[]](0x63A2, 0x6D4B, 0x5DE5, 0x5177))
$appExeName = "$appName.exe"
$outputBaseName = "ProbeTool_Setup_${Arch}_v${Version}"

Push-Location $repoRoot
try {
  & $iscc "/DMyArch=$Arch" "/DMyAppVersion=$Version" "/DMyAppName=$appName" "/DMyAppExeName=$appExeName" "/DMyOutputBaseFilename=$outputBaseName" "$issPath"
} finally {
  Pop-Location
}

Write-Host "Installer build done. Check installer_output directory."

$changelogText = ""
$changelogPath = Join-Path $repoRoot "CHANGELOG.md"
if (Test-Path $changelogPath) {
  $allChangelog = Get-Content -Path $changelogPath -Raw -Encoding UTF8
  $escapedVersion = [regex]::Escape($Version)
  $match = [regex]::Match($allChangelog, "(?ms)^##\s+v?$escapedVersion[^\r\n]*\r?\n(?<body>.*?)(?=^##\s+|\z)")
  if ($match.Success) {
    $changelogText = $match.Groups["body"].Value.Trim()
  }
}

$manifest = [ordered]@{
  version = $Version
  download_url = "$outputBaseName.exe"
  release_url = ""
  changelog = $changelogText
}
$manifestPath = Join-Path $repoRoot "installer_output/latest.json"
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Path $manifestPath -Encoding UTF8
Write-Host "Update manifest created: $manifestPath"
