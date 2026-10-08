#Requires -Version 7.4
param(
    [string]$Cs2Root = 'D:\steam\steamapps\common\Counter-Strike Global Offensive',
    [string]$SteamExecutable,
    [ValidatePattern('^[A-Za-z0-9_]+$')]
    [string]$Map = 'de_dust2',
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
$cs2 = [IO.Path]::GetFullPath((Join-Path $Cs2Root 'game\bin\win64\cs2.exe'))
if (-not (Test-Path -LiteralPath $cs2)) {
    throw "CS2 executable not found: $cs2"
}

$launchArguments = @('-insecure', '-console', '+sv_lan', '1', '+map', $Map)
$steamArguments = @('-applaunch','730') + $launchArguments
$steamProcesses = @(Get-CimInstance Win32_Process -Filter "Name = 'steam.exe'")
if (-not $SteamExecutable -and $steamProcesses.Count -eq 1) { $SteamExecutable = $steamProcesses[0].ExecutablePath }
if (-not $Launch) {
    [pscustomobject]@{ Executable = $cs2; Arguments = $launchArguments; Mode = 'Preview'; ModInstalled = $false
        LaunchRoute='Steam'; SteamExecutable=$SteamExecutable; SteamArguments=$steamArguments }
    return
}
if (Get-Process -Name cs2 -ErrorAction SilentlyContinue) {
    throw 'Close the existing CS2 session yourself before starting the offline lab.'
}
if ($steamProcesses.Count -ne 1 -or -not $SteamExecutable -or $SteamExecutable -ne $steamProcesses[0].ExecutablePath) {
    throw 'Start the installed Steam client first; authentication must be done by the user.'
}
foreach($proxyFile in @('dxgi.dll','ReShade.ini')) {
    if(Test-Path -LiteralPath (Join-Path (Split-Path $cs2) $proxyFile)) { throw 'Restore the temporary ReShade loader/config before a vanilla session.' }
}
Start-Process -FilePath $SteamExecutable -ArgumentList $steamArguments -WindowStyle Hidden | Out-Null
$deadline=[DateTime]::UtcNow.AddSeconds(40)
do {
    $started=@(Get-CimInstance Win32_Process -Filter "Name = 'cs2.exe'")
    if($started.Count){break}
    Start-Sleep -Milliseconds 500
} while([DateTime]::UtcNow -lt $deadline)
if($started.Count -ne 1 -or $started[0].ExecutablePath -ne $cs2 -or $started[0].ParentProcessId -ne $steamProcesses[0].ProcessId) {
    throw 'Steam did not start the exact CS2 executable as its child.'
}
# Steam's China region suffix does not replace the required offline arguments.
$expectedLine='^"?' + [regex]::Escape($cs2) + '"?\s+(?:-steam\s+)?' + [regex]::Escape(($launchArguments -join ' ')) + '(?:\s+-perfectworld)?\s*$'
if($started[0].CommandLine -notmatch $expectedLine){throw 'Steam child arguments differ from the fixed offline arguments; inspect Steam launch options.'}
$owned=Get-Process -Id $started[0].ProcessId
if($owned.WaitForExit(5000)){throw 'Steam CS2 child exited during startup.'}
[pscustomobject]@{ProcessId=$owned.Id;Executable=$cs2;Arguments=$launchArguments;LaunchRoute='Steam';ProcessIdentityVerified=$true;ModInstalled=$false}
