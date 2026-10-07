param([string]$Destination)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Destination) { $Destination = Join-Path $projectRoot '.local\reshade-runtime' }
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$url = 'https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe'
$setup = Join-Path $Destination 'ReShade_Setup_6.8.0_Addon.exe'
$setupSha = 'afe4c8f13048306307983b8b3d41d5bf00a86820440b0e57dea10950e1176445'
$dllSha = '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7'
if (-not (Test-Path -LiteralPath $setup) -or (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant() -ne $setupSha) {
    $download = "$setup.download"
    Invoke-WebRequest -Uri $url -Headers @{ Referer='https://reshade.me/'; Accept='application/octet-stream' } `
        -UserAgent 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36' `
        -OutFile $download -TimeoutSec 30
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $setupSha) { throw 'Official installer hash mismatch.' }
    Move-Item -LiteralPath $download -Destination $setup -Force
}
# The official setup has an appended ZIP. Read one named member as data; never run setup.
$extract = @'
from pathlib import Path
import hashlib,struct,sys,zipfile
setup,destination,expected=map(str,sys.argv[1:])
with zipfile.ZipFile(setup) as archive:
    data=archive.read('ReShade64.dll')
if hashlib.sha256(data).hexdigest()!=expected: raise ValueError('Loader hash mismatch')
if data[:2]!=b'MZ': raise ValueError('Missing PE header')
offset=struct.unpack_from('<I',data,60)[0]
if data[offset:offset+4]!=b'PE\0\0' or struct.unpack_from('<H',data,offset+4)[0]!=0x8664:
    raise ValueError('Loader must be an x64 PE')
Path(destination).write_bytes(data)
'@
$dll = Join-Path $Destination 'ReShade64.dll'
& python -c $extract $setup $dll $dllSha
if ($LASTEXITCODE -ne 0) { throw 'Runtime extraction failed.' }
$version = (Get-Item -LiteralPath $dll).VersionInfo
if ($version.ProductName -ne 'ReShade' -or $version.ProductVersion -ne '6.8.0') { throw 'Unexpected loader product/version.' }
$provenance = [pscustomobject]@{
    Source = $url; InstallerSha256 = $setupSha; LoaderSha256 = $dllSha
    Product = $version.ProductName; ProductVersion = $version.ProductVersion
    InstallerSignatureStatus = (Get-AuthenticodeSignature -LiteralPath $setup).Status.ToString()
    LoaderSignatureStatus = (Get-AuthenticodeSignature -LiteralPath $dll).Status.ToString()
    Validation = 'Official HTTPS download plus pinned content hashes, x64 PE and product/version. No Windows certificate trust claim; no certificate or security setting changes.'
}
$provenance | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'provenance.json') -Encoding utf8
$provenance | ConvertTo-Json
