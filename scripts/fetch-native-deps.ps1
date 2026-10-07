param([string]$Destination)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $Destination) { $Destination = Join-Path $projectRoot '.local\native-deps' }
$files = @(
    @('json/json.hpp','https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp','aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63'),
    @('reshade/reshade.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade.hpp','cf3cd150920ea310b4b1e2e4f11ab8d7221a9fc18755f4891a026563311b524c'),
    @('reshade/reshade_api.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_api.hpp','8d83ec8a620c48da7a20d975bd27b3345b24d3f0d5b78ae4c4fe633d61b804eb'),
    @('reshade/reshade_api_device.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_api_device.hpp','a716c44b6fbb064dbbb0826687728f21aa71ed45e13564e4dc1cdf36da5bdfa0'),
    @('reshade/reshade_api_pipeline.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_api_pipeline.hpp','5bda8e07260c2c60f0fd18a78b11782aed8f69e5d24b5757874614caafdf416f'),
    @('reshade/reshade_api_resource.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_api_resource.hpp','32cdafe6d783813f3ac72ad43e426638d8d0fef56bcb558fad218a14cfb75ee3'),
    @('reshade/reshade_api_format.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_api_format.hpp','68caa17fef712043e810ab88770c15af009aa3c7693e5c115ddc324f2d0809a7'),
    @('reshade/reshade_events.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_events.hpp','2717f56e2bd89af6d708c9c316353d6581c5a41d4526629fadba51d13be57a89'),
    @('reshade/reshade_overlay.hpp','https://raw.githubusercontent.com/crosire/reshade/18deaa52de0c425a78b329e9cb3c497281cd00ec/include/reshade_overlay.hpp','ed914b12fbc41ed038287b0163c0bc934772afcff2c22392044c6008adc5ae79')
)
foreach ($item in $files) {
    $path = Join-Path $Destination $item[0]
    New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $item[2]) {
        $temporary = "$path.download"
        Invoke-WebRequest -Uri $item[1] -OutFile $temporary -TimeoutSec 30
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLower() -ne $item[2]) { throw "Dependency hash mismatch: $($item[0])" }
        Move-Item -LiteralPath $temporary -Destination $path -Force
    }
    Write-Host "Verified $($item[0])"
}
# Headers only: no runtime loader, registry or game-directory writes.
