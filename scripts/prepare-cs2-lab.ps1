param(
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [string]$NativeBuild,
    [string]$Destination,
    [switch]$AllowMissingGame
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $NativeBuild) { $NativeBuild = Join-Path $projectRoot '.local\native-build' }
if (-not $Destination) { $Destination = Join-Path $projectRoot '.local\cs2-lab-candidate' }
$NativeBuild = [IO.Path]::GetFullPath($NativeBuild)
$Destination = [IO.Path]::GetFullPath($Destination)
$gameDirectory = [IO.Path]::GetFullPath((Join-Path $Cs2Root 'game\bin\win64'))
foreach ($path in @($Destination, $NativeBuild, $gameDirectory)) {
    if ($path -match '[\r\n,;]') { throw 'Unsupported path character for ReShade config.' }
}
# This command is preparation only. Never create/modify a directory under the game install.
$gameRoot = [IO.Path]::GetFullPath($Cs2Root).TrimEnd('\') + '\'
if (($Destination.TrimEnd('\') + '\').StartsWith($gameRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Candidate destination must be outside the game install.'
}
$addon = Join-Path $NativeBuild 'CounterCraftProbe.addon64'
$cs2 = Join-Path $gameDirectory 'cs2.exe'
if (-not (Test-Path -LiteralPath $addon -PathType Leaf)) { throw "Missing file: $addon" }
$gameExecutablePresent = Test-Path -LiteralPath $cs2 -PathType Leaf
if (-not $gameExecutablePresent -and -not $AllowMissingGame) { throw "Missing file: $cs2 (use -AllowMissingGame for preparation only)" }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath $addon -Destination (Join-Path $Destination 'CounterCraftProbe.addon64') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'cs2\reshade\CounterCraftProbe.fx') -Destination $Destination -Force
$preset = Join-Path $Destination 'CounterCraftLab.ini'
$runtime = Join-Path $projectRoot '.local\reshade-runtime\ReShade64.dll'
@'
Techniques=CounterCraftProbe@CounterCraftProbe.fx
TechniqueSorting=CounterCraftProbe@CounterCraftProbe.fx
'@ | Set-Content -LiteralPath $preset -Encoding utf8
@"
[ADDON]
AddonPath=$Destination
[GENERAL]
EffectSearchPaths=$Destination
IntermediateCachePath=$Destination\cache
PresetPath=$preset
PerformanceMode=0
[INPUT]
KeyOverlay=36,0,0,0
"@ | Set-Content -LiteralPath (Join-Path $Destination 'ReShade.ini') -Encoding utf8
$targets = foreach ($name in @('dxgi.dll')) {
    $target = Join-Path $gameDirectory $name
    [pscustomobject]@{
        Path = $target
        Exists = Test-Path -LiteralPath $target
        CurrentSha256 = if (Test-Path -LiteralPath $target -PathType Leaf) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { $null }
        Source = if (Test-Path -LiteralPath $runtime -PathType Leaf) { $runtime } else { 'Not supplied: official ReShade 6.8.0 x64 full add-on loader required' }
    }
}
$plan = [pscustomobject]@{
    Mode = 'PreviewOnly'
    GameFilesWritten = $false
    GameExecutablePresent = $gameExecutablePresent
    Executable = $cs2
    Arguments = @('-insecure', '-countercraft-lab', '-countercraft-preview', '-console', '+sv_lan', '1', '+map', 'de_dust2')
    OptionalHostProbeArgument = '-countercraft-host-probe'
    Environment = @{ RESHADE_BASE_PATH_OVERRIDE = $Destination }
    CandidateAddon = Join-Path $Destination 'CounterCraftProbe.addon64'
    CandidateSha256 = (Get-FileHash -LiteralPath (Join-Path $Destination 'CounterCraftProbe.addon64') -Algorithm SHA256).Hash
    Targets = @($targets)
    BackupDirectory = Join-Path $projectRoot '.local\cs2-loader-backup'
    InstallPolicy = 'Requires specific approval. Stop CS2; snapshot target states; refuse an existing loader/config unless reviewed; verify official loader provenance before copying.'
    RestorePolicy = 'Stop CS2; remove only the newly installed, hash-matched dxgi.dll. Keep backup/evidence. Restore before normal CS2 use. Config/logs/cache stay outside the game via the official per-process base-path override.'
    VerifiedScope = 'Offline protocol/socket/GPU/projection/resource-inventory oracles and addon refusal. Upload/FX/pause were verified in CS2 at d1be2d9; the new opt-in host observer requires fresh real-game verification after reinstall. Host camera/depth/world fusion and gameplay remain unverified.'
}
$plan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Destination 'install-plan.json') -Encoding utf8
$plan | ConvertTo-Json -Depth 6
