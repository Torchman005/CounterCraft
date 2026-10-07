#Requires -Version 7.4
param(
    [Parameter(Mandatory=$true)][string]$StateFile,
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$Map='de_dust2',
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
$state = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
if ($state.Mode -ne 'Installed') { throw 'Loader state must be Installed.' }
$gameDirectory = Split-Path ([IO.Path]::GetFullPath($state.Target))
$cs2 = Join-Path $gameDirectory 'cs2.exe'
$candidate = [IO.Path]::GetFullPath($state.Candidate)
if ((Get-FileHash -LiteralPath $state.Target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $state.LoaderSha256) { throw 'Installed loader changed.' }
$launchArguments = @('-insecure','-countercraft-lab','-countercraft-preview','-console','+sv_lan','1','+map',$Map)
if (-not $Launch) {
    [pscustomobject]@{ Mode='Preview'; Executable=$cs2; Arguments=$launchArguments; Environment=@{RESHADE_BASE_PATH_OVERRIDE=$candidate} }
    return
}
if (Get-Process -Name cs2 -ErrorAction SilentlyContinue) { throw 'A CS2 process is already running.' }
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Start the installed Steam client first; authentication must be done by the user.' }
# Official ReShade 6.8 get_base_path override; local to this process, not a registry/user setting.
$consoleLog = Join-Path $candidate ('cs2-console-' + [guid]::NewGuid().ToString('N'))
$process = Start-Process -FilePath $cs2 -WorkingDirectory $gameDirectory -ArgumentList $launchArguments `
    -Environment @{ RESHADE_BASE_PATH_OVERRIDE = $candidate } -WindowStyle Normal `
    -RedirectStandardOutput "$consoleLog.stdout" -RedirectStandardError "$consoleLog.stderr" -PassThru
[pscustomobject]@{ ProcessId=$process.Id; Executable=$cs2; Arguments=$launchArguments; Candidate=$candidate }
