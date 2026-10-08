param(
    [Parameter(Mandatory = $true)][string]$VcVars,
    [string]$Dependencies,
    [string]$BuildDirectory,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $VcVars -PathType Leaf)) { throw 'vcvars64.bat not found.' }
if (-not $Dependencies) { $Dependencies = Join-Path $projectRoot '.local\native-deps' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot '.local\native-build' }
$vsRoot = (Get-Item -LiteralPath $VcVars).Directory.Parent.Parent.Parent.FullName
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
foreach ($path in @($projectRoot, $VcVars, $Dependencies, $BuildDirectory, $cmake, $ninja)) {
    if ($path -match '["%!&|<>^]' -or $path.Contains([char]10) -or $path.Contains([char]13)) { throw 'A build path contains unsupported batch metacharacters.' }
}
foreach ($tool in @($cmake, $ninja, $ctest)) { if (-not (Test-Path -LiteralPath $tool)) { throw "Tool missing: $tool" } }
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$configureReset = ''
$buildReset = if($Clean){'--clean-first'}else{''}
$rules = Join-Path $BuildDirectory 'CMakeFiles\rules.ninja'
if ((Test-Path -LiteralPath $rules) -and (Get-Content -LiteralPath $rules -Raw) -notmatch '(?m)^msvc_deps_prefix = Note: including file:') {
    # CMake can misdecode localized /showIncludes output. Zero recorded header
    # dependencies silently reuse objects with stale C++ class layouts.
    Write-Host 'Resetting localized MSVC dependency metadata and rebuilding all objects.'
    $configureReset = '--fresh'
    $buildReset = '--clean-first'
}
$dependencyLog = Join-Path $BuildDirectory 'header-deps.txt'
$batch = Join-Path $BuildDirectory 'invoke-build.cmd'
@"
@echo off
setlocal
set VSLANG=1033
chcp 65001 >nul
call "$VcVars" >nul
if errorlevel 1 exit /b 1
"$cmake" $configureReset -S "$projectRoot\cs2\native" -B "$BuildDirectory" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOUNTERCRAFT_DEPS="$Dependencies" -DCMAKE_MAKE_PROGRAM="$ninja"
if errorlevel 1 exit /b 1
"$cmake" --build "$BuildDirectory" $buildReset
if errorlevel 1 exit /b 1
"$ninja" -C "$BuildDirectory" -t deps > "$dependencyLog"
if errorlevel 1 exit /b 1
"$ctest" --test-dir "$BuildDirectory" --output-on-failure
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { throw "Native build/tests failed (exit $LASTEXITCODE)." }
$headerDeps = Get-Content -LiteralPath $dependencyLog -Raw
foreach($object in @('countercraft_core.dir/src/depth_inventory.cpp.obj','countercraft_addon.dir/src/addon.cpp.obj','countercraft_addon.dir/src/host_probe.cpp.obj')) {
    $entry = [regex]::Match($headerDeps, [regex]::Escape('CMakeFiles/' + $object).Replace('/','[\\/]') + ': #deps ([0-9]+)')
    if (-not $entry.Success -or [int]$entry.Groups[1].Value -eq 0) { throw "Missing compiler header dependencies for $object; do not use this build." }
}
Write-Host 'Verified MSVC header dependency tracking for inventory and both add-on objects.'
