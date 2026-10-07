param(
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [string]$NativeBuild,
    [string]$Destination
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
foreach ($file in @($addon, $cs2)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing file: $file" } }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath $addon -Destination (Join-Path $Destination 'CounterCraftProbe.addon64') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'cs2\reshade\CounterCraftProbe.fx') -Destination $Destination -Force
$preset = Join-Path $Destination 'CounterCraftLab.ini'
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
$targets = foreach ($name in @('dxgi.dll', 'ReShade.ini')) {
    $target = Join-Path $gameDirectory $name
    [pscustomobject]@{
        Path = $target
        Exists = Test-Path -LiteralPath $target
        CurrentSha256 = if (Test-Path -LiteralPath $target -PathType Leaf) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { $null }
        Source = if ($name -eq 'dxgi.dll') { 'Not supplied: official ReShade 6.8.0 x64 full add-on loader required' } else { Join-Path $Destination $name }
    }
}
$plan = [pscustomobject]@{
    Mode = 'PreviewOnly'
    GameFilesWritten = $false
    Executable = $cs2
    Arguments = @('-insecure', '-countercraft-lab', '-countercraft-preview', '-console', '+sv_lan', '1', '+map', 'de_dust2')
    CandidateAddon = Join-Path $Destination 'CounterCraftProbe.addon64'
    CandidateSha256 = (Get-FileHash -LiteralPath (Join-Path $Destination 'CounterCraftProbe.addon64') -Algorithm SHA256).Hash
    Targets = @($targets)
    BackupDirectory = Join-Path $projectRoot '.local\cs2-loader-backup'
    InstallPolicy = 'Requires specific approval. Stop CS2; snapshot target states; refuse an existing loader/config unless reviewed; verify official loader provenance before copying.'
    RestorePolicy = 'Stop CS2; restore backed-up originals or remove only the two newly installed, hash-matched target files. Keep backups/evidence. Restore before normal CS2 use.'
    VerifiedScope = 'Native protocol/socket/GPU oracle and addon compile/refusal. No real CS2 callback, FX compile, host camera/depth or gameplay validation yet.'
}
$plan | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Destination 'install-plan.json') -Encoding utf8
$plan | ConvertTo-Json -Depth 6
