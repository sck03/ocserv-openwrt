[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$RemoveStoppedTestEnvironment
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

# Keep the installed Windows toolchains, Python test tools and pinned source archives.
foreach ($relative in @(
    '.tools\client-native-rebuild', '.tools\patch-check',
    '.tools\upstream-openconnect-gui-1.6.2',
    '.tools\downloads\openssl-3.5.8.tar.gz',
    '.tools\downloads\client-d9edec6-source-artifact.zip',
    '.tools\downloads\client-d9edec6-x64-artifact.zip',
    '.tools\downloads\client-d9edec6-x86-artifact.zip',
    '.deps', 'build',
    'dist\BulijieVPN-0.5.0-source.zip', 'dist\SHA256SUMS-source.txt',
    'scripts\__pycache__', 'tests\__pycache__', 'client\tests\__pycache__'
)) { Add-Candidate $relative }

$review = '.tools\cleanup-review-20260916-072040'
if ($RemoveStoppedTestEnvironment) {
    Add-Candidate $review
    Add-Candidate '.tools\server-fix-20260917'
    Add-Candidate '.tools\downloads\debian-12-genericcloud-amd64.qcow2'
    foreach ($name in @(
        'check-server-ui.py', 'github-api.ps1', 'github-server-release-parallel.py',
        'github-server-release-v041.py', 'github-server-release.py', 'package-vpn-guide.py',
        'public-release-SHA256SUMS.txt', 'qemu-extract.log', 'release-v0.3.0-notes.md',
        'release-v0.3.0-state.json', 'server_preview_packaged.py', 'server-release-state.json',
        'setup-builder.py', 'verify-server-artifacts.py'
    )) { Add-Candidate ('.tools\' + $name) }
} elseif (Test-Path -LiteralPath (Join-Path $cleanupRoot "$review\items")) {
    Get-ChildItem -LiteralPath (Join-Path $cleanupRoot "$review\items") -Force |
        Where-Object { $_.Name -match '^\d{3}-' -and $_.Name -notmatch '^(027|028|077|078)-' } |
        ForEach-Object { Add-Candidate ("$review\items\" + $_.Name) }
}

if (Test-Path -LiteralPath (Join-Path $cleanupRoot 'test-results')) {
    Get-ChildItem -LiteralPath (Join-Path $cleanupRoot 'test-results') -Force |
        Where-Object {
            $_.Name -match 'v040|native-|^imports-x|^release-imports|^client-local|^client-debug|^server-ui-final' -or
            (!$_.PSIsContainer -and $_.Extension -eq '.log')
        } | ForEach-Object { Add-Candidate ('test-results\' + $_.Name) }
}

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

$backups = @()
if ($RemoveStoppedTestEnvironment) {
    foreach ($entry in @(
        @('027-N1-VPN-ONLY-config-20260915.zip', 'N1-VPN-ONLY-config-20260915.zip'),
        @('028-N1-VPN-ONLY-config-20260915.zip.sha256.txt', 'N1-VPN-ONLY-config-20260915.zip.sha256.txt')
    )) {
        $source = Assert-WorkspacePath (Join-Path $cleanupRoot ("$review\items\" + $entry[0]))
        $destination = Assert-WorkspacePath (Join-Path $cleanupRoot ('artifacts\backups\' + $entry[1]))
        if (Test-Path -LiteralPath $source) {
            $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            if ((Test-Path -LiteralPath $destination) -and
                (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
                throw "An existing configuration backup differs: $destination"
            }
            $backups += [pscustomobject]@{ Source = $source; Destination = $destination; Hash = $hash }
        }
    }
}

$plan | Sort-Object Bytes -Descending |
    Select-Object Relative, @{Name='MiB'; Expression={[math]::Round($_.Bytes / 1MB, 2)}} |
    Format-Table -AutoSize
$totalBytes = ($plan | Measure-Object -Property Bytes -Sum).Sum
Write-Output ('Selected {0} targets, {1:N3} GiB.' -f $plan.Count, ($totalBytes / 1GB))
if ($backups.Count) { Write-Output 'The N1 configuration backup will be preserved in artifacts\backups.' }
if (!$Apply) {
    Write-Output 'Preview only. Add -Apply to delete the listed files.'
    return
}

# Preserve the small user configuration backup before removing the old review folder.
foreach ($backup in $backups) {
    $destination = Assert-WorkspacePath $backup.Destination
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    if (!(Test-Path -LiteralPath $destination)) {
        Copy-Item -LiteralPath $backup.Source -Destination $destination
    }
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $backup.Hash) {
        throw "Configuration backup verification failed: $destination"
    }
}
# ==================== 替换后的部分 ====================
foreach ($target in $plan) {
    $resolved = (Resolve-Path -LiteralPath (Assert-WorkspacePath $target.Path)).Path
    if ($resolved -ne $target.Path) { throw "A reviewed path changed: $($target.Path)" }

    # 1. 执行强制删除
    Remove-Item -LiteralPath $resolved -Recurse -Force

    # 2. Windows 异步删除缓冲与重试机制（最多等待 3 秒）
    $retryCount = 0
    $stillExists = $false
    do {
        try {
            # 使用 -ErrorAction SilentlyContinue 屏蔽正在删除时引发的“拒绝访问”报错
            $stillExists = Test-Path -LiteralPath $resolved -ErrorAction SilentlyContinue
        } catch {
            $stillExists = $true
        }

        if ($stillExists -and $retryCount -lt 15) {
            Start-Sleep -Milliseconds 200
            $retryCount++
        } else {
            break
        }
    } while ($stillExists)

    # 3. 最终确认
    if ($stillExists) {
        throw "Cleanup did not finish (Path is locked or inaccessible): $resolved"
    }
}
# ====================================================

Write-Output ('Deleted {0:N3} GiB of unused files.' -f ($totalBytes / 1GB))
Write-Output 'Current release packages, source files and Windows toolchains were retained.'\n
