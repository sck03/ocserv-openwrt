param(
    [ValidateSet('x64','x86','all')][string]$Architecture = 'all',
    [string]$MsysRoot = 'C:\msys64',
    [string]$OutputRoot = 'build/client',
    [ValidateRange(0,256)][int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cmake = Join-Path $MsysRoot 'ucrt64\bin\cmake.exe'
$ninja = Join-Path $MsysRoot 'ucrt64\bin\ninja.exe'
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
foreach ($tool in @($cmake,$ninja,$bash)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool; see docs/BUILD.md." }
}
$targets = if ($Architecture -eq 'all') { @('x64','x86') } else { @($Architecture) }
$parallelJobs = if ($Jobs) { $Jobs } else { [Environment]::ProcessorCount }
$previousJobs = $env:BUILD_JOBS
try {
    $env:BUILD_JOBS = "$parallelJobs"
    foreach ($target in $targets) {
        $prefix = Join-Path $projectRoot ".deps/$target"
        & $bash -l (Join-Path $PSScriptRoot 'build-dependencies.sh').Replace('\','/') $target
        if ($LASTEXITCODE) { throw "Dependency build failed: $target" }
        $build = Join-Path (Join-Path $projectRoot $OutputRoot) $target
        & $cmake --fresh -S $projectRoot -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$prefix/toolchain.cmake" "-DBRIDGE_DEPS=$prefix" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' '-DBUILD_TESTING=ON'
        if ($LASTEXITCODE) { throw "CMake configure failed: $target" }
        & $cmake --build $build --parallel $parallelJobs
        if ($LASTEXITCODE) { throw "Compile failed: $target" }
        python (Join-Path $PSScriptRoot 'test-client.py') --build $build --output (Join-Path $projectRoot "test-results/client-$target")
        if ($LASTEXITCODE) { throw "Client tests failed: $target" }
    }
} finally {
    $env:BUILD_JOBS = $previousJobs
}
