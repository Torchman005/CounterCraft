param(
    [ValidateSet('Preview','Install','Restore')][string]$Mode = 'Preview',
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [string]$Candidate,
    [string]$Loader,
    [string]$BackupSnapshot,
    [string]$StateFile
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Candidate) { $Candidate = Join-Path $projectRoot '.local\cs2-lab-candidate' }
if (-not $Loader) { $Loader = Join-Path $projectRoot '.local\reshade-runtime\ReShade64.dll' }
$Cs2Root = [IO.Path]::GetFullPath($Cs2Root)
$gameDirectory = [IO.Path]::GetFullPath((Join-Path $Cs2Root 'game\bin\win64'))
$target = Join-Path $gameDirectory 'dxgi.dll'
$expectedSha = '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'
function Get-Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
if ($Mode -eq 'Preview') {
    [pscustomobject]@{ Mode=$Mode; Target=$target; Exists=(Test-Path -LiteralPath $target); Loader=$Loader; Candidate=$Candidate }
    return
}
if (Get-Process -Name cs2 -ErrorAction SilentlyContinue) { throw 'Close the existing CS2 process before changing its loader.' }
if (-not (Test-Path -LiteralPath (Join-Path $gameDirectory 'cs2.exe') -PathType Leaf)) { throw 'Missing CS2 executable.' }

if ($Mode -eq 'Restore') {
    if (-not $StateFile) { throw 'Restore requires the exact recorded state file.' }
    $state = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
    if ($state.Mode -ne 'Installed' -or $state.OriginalExisted -ne $false -or
        [IO.Path]::GetFullPath($state.Target) -ne $target -or $state.LoaderSha256 -ne $expectedSha) {
        throw 'Installation state does not authorize this exact target.'
    }
    if (Test-Path -LiteralPath $target) {
        if ((Get-Sha $target) -ne $expectedSha) { throw 'Target changed; refusing to remove a different loader.' }
        # The exact nonrecursive path is checked above and lies in the explicitly named game directory.
        Copy-Item -LiteralPath $target -Destination (Join-Path (Split-Path $StateFile) 'removed-loader.dll') -Force
        Remove-Item -LiteralPath $target
    }
    $state.Mode = 'Restored'
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $StateFile -Encoding utf8
    $state
    return
}

if (Test-Path -LiteralPath $target) { throw 'Existing dxgi.dll; refusing to overwrite a loader.' }
if (Test-Path -LiteralPath (Join-Path $gameDirectory 'ReShade.ini')) {
    throw 'Existing game-folder ReShade.ini can override the isolated base path; review it first.'
}
if (-not $BackupSnapshot -or -not (Test-Path -LiteralPath $BackupSnapshot -PathType Leaf)) { throw 'A completed universal-modder backup ZIP is required.' }
$zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($BackupSnapshot))
try {
    $entry = $zip.GetEntry('_um_manifest.json')
    if (-not $entry) { throw 'Backup has no universal-modder manifest.' }
    $reader = [IO.StreamReader]::new($entry.Open())
    try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ([IO.Path]::GetFullPath($manifest.source) -ne $gameDirectory -or
        $manifest.files.PSObject.Properties.Name -contains 'dxgi.dll') { throw 'Backup is not the pre-loader snapshot for this exact directory.' }
} finally { $zip.Dispose() }
if ((Get-Sha $Loader) -ne $expectedSha) { throw 'Official x64 full-add-on loader hash mismatch.' }
$plan = Get-Content -LiteralPath (Join-Path $Candidate 'install-plan.json') -Raw | ConvertFrom-Json
if ([IO.Path]::GetFullPath($plan.Executable) -ne (Join-Path $gameDirectory 'cs2.exe') -or
    (Get-Sha $plan.CandidateAddon) -ne $plan.CandidateSha256.ToLowerInvariant()) { throw 'Candidate plan/addon does not match.' }
if (-not (Test-Path -LiteralPath (Join-Path $Candidate 'ReShade.ini'))) { throw 'Missing isolated config.' }
if (-not $StateFile) {
    $StateFile = Join-Path $projectRoot ('.local\cs2-loader-backup\' + [guid]::NewGuid().ToString('N') + '\install-state.json')
}
$StateFile = [IO.Path]::GetFullPath($StateFile)
if (Test-Path -LiteralPath $StateFile) { throw 'State file already exists; do not overwrite installation history.' }
New-Item -ItemType Directory -Path (Split-Path $StateFile) -Force | Out-Null
$state = [pscustomobject]@{
    Mode='Prepared'; Target=$target; OriginalExisted=$false; LoaderSha256=$expectedSha
    BackupSnapshot=[IO.Path]::GetFullPath($BackupSnapshot); Candidate=[IO.Path]::GetFullPath($Candidate)
    StateFile=$StateFile; CreatedUtc=[DateTime]::UtcNow.ToString('o')
}
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $StateFile -Encoding utf8
# CreateNew refuses a race with an existing loader. Record state before writing.
$source = [IO.File]::OpenRead($Loader)
try {
    $destination = [IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $source.CopyTo($destination) } finally { $destination.Dispose() }
} finally { $source.Dispose() }
if ((Get-Sha $target) -ne $expectedSha) { throw 'Installed hash mismatch; inspect the recorded Prepared state before recovery.' }
$state.Mode = 'Installed'
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $StateFile -Encoding utf8
$state
