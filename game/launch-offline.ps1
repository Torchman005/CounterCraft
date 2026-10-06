param(
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [ValidatePattern('^[A-Za-z0-9_]+$')]
    [string]$Map = 'de_dust2',
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$cs2 = Join-Path $Cs2Root 'game\bin\win64\cs2.exe'
if (-not (Test-Path -LiteralPath $cs2)) {
    throw "CS2 executable not found: $cs2"
}

$launchArguments = @('-insecure', '-console', '+sv_lan', '1', '+map', $Map)
if (-not $Launch) {
    [pscustomobject]@{ Executable = $cs2; Arguments = $launchArguments; Mode = 'Preview'; ModInstalled = $false }
    return
}
if (Get-Process -Name cs2 -ErrorAction SilentlyContinue) {
    throw 'Close the existing CS2 session yourself before starting the offline lab.'
}
Write-Host 'Starting vanilla CS2 offline. The CounterCraft renderer is not installed yet.'
Write-Host 'Use this session only for local testing; do not connect to matchmaking.'
Start-Process -FilePath $cs2 -WorkingDirectory (Split-Path $cs2) -ArgumentList $launchArguments
