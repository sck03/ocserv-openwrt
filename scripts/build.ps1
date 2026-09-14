param(
    [ValidateSet('x64','x86','all')][string]$Architecture = 'all',
    [string]$MsysRoot = 'D:\msys64',
    [string]$OutputRoot = 'build',
    [switch]$SkipDependencies
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmake = Join-Path $MsysRoot 'ucrt64\bin\cmake.exe'
$ninja = Join-Path $MsysRoot 'ucrt64\bin\ninja.exe'
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
if (!(Test-Path -LiteralPath $cmake)) { throw 'Install the MSYS2 CMake/Ninja build tools first; see docs/BUILD.md.' }
$targets = if ($Architecture -eq 'all') { @('x64','x86') } else { @($Architecture) }
foreach ($target in $targets) {
    $compilerRoot = (Join-Path $projectRoot ".tools\$target\w64devkit\bin").Replace('\','/')
    $dependency = Join-Path $projectRoot ".deps\$target\lib\libopenconnect.a"
    if (!(Test-Path -LiteralPath $dependency)) {
        if ($SkipDependencies) { throw "Missing static dependencies: $dependency" }
        $script = (Join-Path $PSScriptRoot 'build-deps.sh').Replace('\','/')
        & $bash -l $script $target
        if ($LASTEXITCODE) { throw "Dependency build failed: $target" }
    }
    $build = Join-Path (Join-Path $projectRoot $OutputRoot) $target
    & $cmake --fresh -S $projectRoot -B $build -G Ninja "-DCMAKE_C_COMPILER=$compilerRoot/gcc.exe" "-DCMAKE_CXX_COMPILER=$compilerRoot/g++.exe" "-DCMAKE_RC_COMPILER=$compilerRoot/windres.exe" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTING=ON'
    if ($LASTEXITCODE) { throw "CMake configure failed: $target" }
    & $cmake --build $build --parallel 6
    if ($LASTEXITCODE) { throw "Compile failed: $target" }
    & (Join-Path $MsysRoot 'ucrt64\bin\ctest.exe') --test-dir $build --output-on-failure
    if ($LASTEXITCODE) { throw "Tests failed: $target" }
}
