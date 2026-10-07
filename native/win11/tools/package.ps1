# Lays out the Windows 11 zip:
#   Pulse.exe     the launcher (starts data\Pulse.exe)
#   data\         the app and the Windows App SDK files it loads
#   lang\<code>\  one folder per Pulse language (Resources.resw)
# Usage: tools/package.ps1 -App publish/win-x64 -Launcher publish/launcher-win-x64 -Out package/win-x64
param(
    [Parameter(Mandatory)] [string] $App,
    [Parameter(Mandatory)] [string] $Launcher,
    [Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
$data = Join-Path $Out 'data'
$lang = Join-Path $Out 'lang'
New-Item -ItemType Directory -Force $data, $lang | Out-Null

Copy-Item (Join-Path $Launcher 'Pulse.exe') $Out
Copy-Item -Recurse (Join-Path $App '*') $data -Exclude 'Strings', '*.pdb'
Get-ChildItem -Directory (Join-Path $App 'Strings') | ForEach-Object { Copy-Item -Recurse $_.FullName $lang }

$top = Get-ChildItem $Out
$stray = $top | Where-Object { -not $_.PSIsContainer -and $_.Name -ne 'Pulse.exe' }
if ($stray) { throw "Unexpected files at the top of the package: $($stray.Name -join ', ')" }
if (-not (Test-Path (Join-Path $data 'Pulse.exe'))) { throw 'data\Pulse.exe missing' }
if (-not (Get-ChildItem -Recurse $lang -Filter Resources.resw)) { throw 'no languages in lang\' }
$top | Select-Object Name
