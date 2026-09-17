# Delegate package contents, licensing and audits to the same packager as CI.
param(
    [ValidateSet('x64','x86','all')][string]$Architecture = 'all',
    [string]$BuildRoot = 'build/client',
    [switch]$SkipSource
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$targets = if ($Architecture -eq 'all') { @('x64','x86') } else { @($Architecture) }
$checksums = @()
foreach ($target in $targets) {
    python (Join-Path $PSScriptRoot 'package-client.py') --arch $target --build (Join-Path (Join-Path $projectRoot $BuildRoot) $target) --strip (Join-Path $projectRoot ".tools/$target/w64devkit/bin/strip.exe")
    if ($LASTEXITCODE) { throw "Client packaging failed: $target" }
    $checksums += Get-Content -LiteralPath (Join-Path $projectRoot "dist/SHA256SUMS-windows-$target.txt")
}
if (!$SkipSource) {
    python (Join-Path $PSScriptRoot 'package-source.py')
    if ($LASTEXITCODE) { throw 'Source packaging failed.' }
    $checksums += Get-Content -LiteralPath (Join-Path $projectRoot 'dist/SHA256SUMS-source.txt')
}
[IO.File]::WriteAllLines((Join-Path $projectRoot 'dist/SHA256SUMS.txt'), $checksums, [Text.Encoding]::ASCII)
