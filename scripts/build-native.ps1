param(
    [Parameter(Mandatory = $true)][string]$VcVars,
    [string]$Dependencies,
    [string]$BuildDirectory
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
$batch = Join-Path $BuildDirectory 'invoke-build.cmd'
@"
@echo off
setlocal
call "$VcVars" >nul
if errorlevel 1 exit /b 1
"$cmake" -S "$projectRoot\cs2\native" -B "$BuildDirectory" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOUNTERCRAFT_DEPS="$Dependencies" -DCMAKE_MAKE_PROGRAM="$ninja"
if errorlevel 1 exit /b 1
"$cmake" --build "$BuildDirectory"
if errorlevel 1 exit /b 1
"$ctest" --test-dir "$BuildDirectory" --output-on-failure
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
& $env:ComSpec /d /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { throw "Native build/tests failed (exit $LASTEXITCODE)." }
