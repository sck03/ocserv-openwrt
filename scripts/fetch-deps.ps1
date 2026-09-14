param([string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$downloads = Join-Path $projectRoot '.tools\downloads'
$sources = Join-Path $projectRoot '.deps\sources'
New-Item -ItemType Directory -Path $downloads,$sources -Force | Out-Null
if (!(Test-Path -LiteralPath $SevenZip)) { throw '7-Zip is required on the development machine for source extraction.' }
$items = @(
    @('openconnect-9.21.tar.gz','https://www.infradead.org/openconnect/download/openconnect-9.21.tar.gz','5b32369467db6e5f317aa1ed12cfcbb81ed00bdbc765450b6bfcbdc300944a58','source'),
    @('openssl-3.5.8.tar.gz','https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz','a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2','source'),
    @('libxml2-2.15.3.tar.xz','https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.3.tar.xz','78262a6e7ac170d6528ebfe2efccdf220191a5af6a6cd61ea4a9a9a5042c7a07','source'),
    @('zlib-1.3.2.tar.gz','https://zlib.net/zlib-1.3.2.tar.gz','bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16','source'),
    @('wintun-0.14.1.zip','https://www.wintun.net/builds/wintun-0.14.1.zip','07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51','wintun'),
    @('w64devkit-x64-2.10.0.7z.exe','https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe','18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e','x64'),
    @('w64devkit-x86-2.10.0.7z.exe','https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x86-2.10.0.7z.exe','513ff7fc571cf6764a5aedbca92f43c513f8d01085c65df5133a82db13a7e63d','x86')
)
foreach ($item in $items) {
    $file = Join-Path $downloads $item[0]
    if (!(Test-Path -LiteralPath $file)) { Invoke-WebRequest -Uri $item[1] -OutFile $file -TimeoutSec 300 }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $item[2]) { throw "SHA-256 mismatch: $($item[0])" }
    if ($item[3] -eq 'source') {
        & $SevenZip x $file "-o$downloads" -y | Out-Null
        if ($LASTEXITCODE) { throw 'Archive extraction failed' }
        $tar = Join-Path $downloads ($item[0] -replace '\.(gz|xz)$','')
        & $SevenZip x $tar "-o$sources" -y | Out-Null
    } elseif ($item[3] -eq 'wintun') {
        Expand-Archive -LiteralPath $file -DestinationPath $sources -Force
    } else {
        & $SevenZip x $file "-o$(Join-Path $projectRoot ('.tools\' + $item[3]))" -y | Out-Null
    }
    if ($LASTEXITCODE) { throw "Extraction failed: $($item[0])" }
    Write-Output "Verified $($item[0])"
}
