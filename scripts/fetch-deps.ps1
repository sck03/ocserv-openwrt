# Windows build tools only. All library versions/checksums live in sources.json.
param(
    [ValidateSet('x64','x86','all')][string]$Architecture = 'all',
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$downloads = Join-Path $projectRoot '.tools/downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
if (!(Test-Path -LiteralPath $SevenZip)) { throw 'Install 7-Zip on the development machine or supply -SevenZip.' }
$checksums = @{
    x64 = '18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e'
    x86 = '513ff7fc571cf6764a5aedbca92f43c513f8d01085c65df5133a82db13a7e63d'
}
$targets = if ($Architecture -eq 'all') { @('x64','x86') } else { @($Architecture) }
foreach ($target in $targets) {
    $name = "w64devkit-$target-2.10.0.7z.exe"
    $file = Join-Path $downloads $name
    if (!(Test-Path -LiteralPath $file)) {
        Invoke-WebRequest -Uri "https://github.com/skeeto/w64devkit/releases/download/v2.10.0/$name" -OutFile "$file.part" -TimeoutSec 300
        if ((Get-FileHash -LiteralPath "$file.part" -Algorithm SHA256).Hash -ne $checksums[$target]) { throw "SHA-256 mismatch: $name" }
        Move-Item -LiteralPath "$file.part" -Destination $file
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $checksums[$target]) { throw "SHA-256 mismatch: $name" }
    $destination = Join-Path $projectRoot ".tools/$target"
    & $SevenZip x $file "-o$destination" -y | Out-Null
    if ($LASTEXITCODE) { throw "Toolchain extraction failed: $target" }
    Write-Output "Verified w64devkit 2.10.0 ($target)"
}
python (Join-Path $PSScriptRoot 'fetch-sources.py')
if ($LASTEXITCODE) { throw 'Source verification or extraction failed.' }
