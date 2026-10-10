[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$IncludeDependencies,
    [switch]$IncludeTestResults
)

$ErrorActionPreference = 'Stop'
$cleanupRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$cleanupPrefix = $cleanupRoot + '\'
if (!(Test-Path -LiteralPath (Join-Path $cleanupRoot 'client\CMakeLists.txt')) -or
    !(Test-Path -LiteralPath (Join-Path $cleanupRoot 'scripts\sources.json'))) {
    throw 'Run this script from the OpenVPN project scripts directory.'
}

function Assert-WorkspacePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (!$full.StartsWith($cleanupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the project, or is the project root: $full"
    }
    $ancestor = $full
    while ($ancestor.Length -ge $cleanupRoot.Length) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a symbolic link or junction: $ancestor"
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $full
}

$candidates = [System.Collections.Generic.List[string]]::new()
function Add-Candidate([string]$Relative) {
    $path = Assert-WorkspacePath (Join-Path $cleanupRoot $Relative)
    if ((Test-Path -LiteralPath $path) -and !$candidates.Contains($path)) {
        $candidates.Add($path)
    }
}

# Keep release packages, backups, test environments, toolchains and source archives.
foreach ($relative in @(
    'build', '.ruff_cache', 'scripts\__pycache__', 'tests\__pycache__', 'client\tests\__pycache__'
)) { Add-Candidate $relative }
if ($IncludeDependencies) { Add-Candidate '.deps' }
if ($IncludeTestResults) { Add-Candidate 'test-results' }

$tracked = @(& git -C $cleanupRoot ls-files)
if ($LASTEXITCODE) { throw 'Cannot check tracked files; no files were deleted.' }
$processes = @(Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID })
$plan = @(foreach ($path in $candidates) {
    $relative = $path.Substring($cleanupPrefix.Length)
    $gitPath = $relative.Replace('\', '/')
    if (@($tracked | Where-Object { $_ -eq $gitPath -or $_.StartsWith($gitPath + '/') }).Count) {
        throw "A target contains tracked source files: $relative"
    }
    foreach ($process in $processes) {
        $command = [string]$process.CommandLine
        $executable = [string]$process.ExecutablePath
        if ($executable.Equals($path, [StringComparison]::OrdinalIgnoreCase) -or
            $executable.StartsWith($path + '\', [StringComparison]::OrdinalIgnoreCase) -or
            $command.Replace('/', '\').IndexOf($path, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "A running process uses $relative (PID $($process.ProcessId)). Stop it before cleaning."
        }
    }
    $item = Get-Item -LiteralPath $path -Force
    if ($item.PSIsContainer) {
        $entries = @(Get-ChildItem -LiteralPath $path -Recurse -Force)
        if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
            throw "A target contains a symbolic link or junction: $relative"
        }
        $bytes = ($entries | Where-Object { !$_.PSIsContainer } | Measure-Object -Property Length -Sum).Sum
    } else { $bytes = $item.Length }
    [pscustomobject]@{ Path = $path; Relative = $relative; Bytes = [long]$bytes }
})

$plan | Sort-Object Bytes -Descending |
    Select-Object Relative, @{Name='MiB'; Expression={[math]::Round($_.Bytes / 1MB, 2)}} |
    Format-Table -AutoSize
$totalBytes = ($plan | Measure-Object -Property Bytes -Sum).Sum
Write-Output ('Selected {0} targets, {1:N3} GiB.' -f $plan.Count, ($totalBytes / 1GB))
if (!$Apply) {
    Write-Output 'Preview only. Add -Apply to delete the listed files.'
    return
}

foreach ($target in $plan) {
    $resolved = (Resolve-Path -LiteralPath (Assert-WorkspacePath $target.Path)).Path
    if ($resolved -ne $target.Path) { throw "A reviewed path changed: $($target.Path)" }
    Remove-Item -LiteralPath $resolved -Recurse -Force
    if (Test-Path -LiteralPath $resolved) { throw "Cleanup did not finish: $resolved" }
}
Write-Output ('Deleted {0:N3} GiB of generated files.' -f ($totalBytes / 1GB))
Write-Output 'Release packages, backups, source files and toolchains were retained.'
