param(
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [string]$MinecraftRoot = 'D:\pcl2\Release 2.8.3\.minecraft',
    [ValidatePattern('^[^\\/:*?"<>|]+$')]
    [string]$MinecraftVersion = '1.20.1-OptiFine_I6'
)
$ErrorActionPreference = 'Stop'
$versionJson = Join-Path $MinecraftRoot "versions\$MinecraftVersion\$MinecraftVersion.json"
$profile = if (Test-Path -LiteralPath $versionJson) {
    Get-Content -LiteralPath $versionJson -Raw | ConvertFrom-Json
} else { $null }
[pscustomobject]@{
    Cs2Executable = Test-Path -LiteralPath (Join-Path $Cs2Root 'game\bin\win64\cs2.exe')
    Source2Engine = Test-Path -LiteralPath (Join-Path $Cs2Root 'game\bin\win64\engine2.dll')
    MinecraftProfile = $profile.id
    MinecraftMainClass = $profile.mainClass
    MetamodDirectoryPresent = Test-Path -LiteralPath (Join-Path $Cs2Root 'game\csgo\addons\metamod')
    CounterStrikeSharpDirectoryPresent = Test-Path -LiteralPath (Join-Path $Cs2Root 'game\csgo\addons\counterstrikesharp')
    CounterCraftRendererImplemented = $false
}
