[CmdletBinding()]
param(
    [switch]$Apply,
    [switch]$IncludeDependencies,
    [switch]$IncludeTestResults,
    [switch]$DeepClean
)

$ErrorActionPreference = 'Stop'
$cleanupRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\')
$cleanupPrefix = $cleanupRoot + '\'
if (!(Test-Path -LiteralPath (Join-Path $cleanupRoot 'client\CMakeLists.txt')) -or
    !(Test-Path -LiteralPath (Join-Path $cleanupRoot 'scripts\sources.json'))) {
    throw 'Run this script from the project scripts directory.'
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

function Test-PendingDirectoryDeletion([string]$Path) {
    if (!('LansyraMaintenance.DirectoryDeletion' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace LansyraMaintenance {
    public static class DirectoryDeletion {
        [StructLayout(LayoutKind.Sequential)]
        private struct UnicodeString { public ushort Length, MaximumLength; public IntPtr Buffer; }
        [StructLayout(LayoutKind.Sequential)]
        private struct ObjectAttributes {
            public int Length; public IntPtr RootDirectory, ObjectName;
            public uint Attributes; public IntPtr SecurityDescriptor, SecurityQualityOfService;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct IoStatus { public IntPtr Status, Information; }
        [DllImport("ntdll.dll")]
        private static extern int NtOpenFile(out IntPtr handle, uint access,
            ref ObjectAttributes attributes, out IoStatus status, uint sharing, uint options);
        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr handle);
        public static bool IsPending(string path) {
            string nativePath = @"\??\" + path;
            IntPtr text = Marshal.StringToHGlobalUni(nativePath);
            IntPtr name = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(UnicodeString)));
            IntPtr handle = IntPtr.Zero;
            try {
                ushort length = checked((ushort)(nativePath.Length * 2));
                Marshal.StructureToPtr(new UnicodeString {
                    Length = length, MaximumLength = length, Buffer = text
                }, name, false);
                var attributes = new ObjectAttributes {
                    Length = Marshal.SizeOf(typeof(ObjectAttributes)), ObjectName = name,
                    Attributes = 0x40
                };
                IoStatus status;
                // Read attributes only; share deletion and never follow a reparse point.
                int result = NtOpenFile(out handle, 0x80, ref attributes, out status, 7, 0x200001);
                return result == unchecked((int)0xC0000056); // STATUS_DELETE_PENDING
            } finally {
                if (handle != IntPtr.Zero) CloseHandle(handle);
                Marshal.FreeHGlobal(name);
                Marshal.FreeHGlobal(text);
            }
        }
    }
}
'@
    }
    return [LansyraMaintenance.DirectoryDeletion]::IsPending($Path)
}

$candidates = [System.Collections.Generic.List[string]]::new()
$pendingDirectories = [System.Collections.Generic.List[string]]::new()
function Add-Candidate([string]$Relative) {
    $path = Assert-WorkspacePath (Join-Path $cleanupRoot $Relative)
    if ((Test-Path -LiteralPath $path) -and !$candidates.Contains($path)) {
        $candidates.Add($path)
    }
}

# Always retain release packages, backups and private configurations.
foreach ($relative in @(
    'build', '.ruff_cache', 'scripts\__pycache__', 'tests\__pycache__', 'client\tests\__pycache__',
    'dist\validation-x64', 'dist\validation-x86',
    'mobile\ios\Lansyra.xcodeproj', 'mobile\ios\App\Info.plist', 'mobile\ios\Tunnel\Info.plist',
    'desktop\macos\Lansyra.xcodeproj', 'desktop\macos\App\Info.plist',
    'desktop\macos\Tunnel\Info.plist', 'desktop\macos\App\AppIcon.icns'
)) { Add-Candidate $relative }
if ($IncludeDependencies -or $DeepClean) { Add-Candidate '.deps' }
if ($IncludeTestResults -or $DeepClean) { Add-Candidate 'test-results' }
if ($DeepClean) {
    # All of these can be downloaded or generated again using the build guide.
    foreach ($relative in @('.tools', '.reference', '.venv', 'artifacts\validation')) {
        Add-Candidate $relative
    }
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
    try {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer) {
            $entries = @(Get-ChildItem -LiteralPath $path -Recurse -Force)
            if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
                throw "A target contains a symbolic link or junction: $relative"
            }
            $bytes = ($entries | Where-Object { !$_.PSIsContainer } | Measure-Object -Property Length -Sum).Sum
        } else { $bytes = $item.Length }
    } catch {
        if (Test-PendingDirectoryDeletion $path) {
            $pendingDirectories.Add($relative)
            continue
        }
        throw
    }
    [pscustomobject]@{ Path = $path; Relative = $relative; Bytes = [long]$bytes }
})

$plan | Sort-Object Bytes -Descending |
    Select-Object Relative, @{Name='MiB'; Expression={[math]::Round($_.Bytes / 1MB, 2)}} |
    Format-Table -AutoSize
$totalBytes = ($plan | Measure-Object -Property Bytes -Sum).Sum
Write-Output ('Selected {0} targets, {1:N3} GiB.' -f $plan.Count, ($totalBytes / 1GB))
if (!$Apply) {
    foreach ($relative in $pendingDirectories) { Write-Output "Windows directory deletion pending: $relative" }
    Write-Output 'Preview only. Add -Apply to delete the listed files.'
    return
}

foreach ($target in $plan) {
    $resolved = (Resolve-Path -LiteralPath (Assert-WorkspacePath $target.Path)).Path
    if ($resolved -ne $target.Path) { throw "A reviewed path changed: $($target.Path)" }
    # Directory watchers can retain an empty directory after Windows accepts deletion.
    # Only the exact native pending-deletion status is deferred; access errors still fail.
    for ($attempt = 0; $attempt -lt 3 -and (Test-Path -LiteralPath $resolved); $attempt++) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue -ErrorVariable cleanupErrors
        if (Test-Path -LiteralPath $resolved) { Start-Sleep -Milliseconds 200 }
    }
    if (Test-Path -LiteralPath $resolved) {
        if (Test-PendingDirectoryDeletion $resolved) { $pendingDirectories.Add($target.Relative) }
        else { throw "Cleanup did not finish: $resolved; $($cleanupErrors[0])" }
    }
}
Write-Output ('Deleted {0:N3} GiB of generated files.' -f ($totalBytes / 1GB))
foreach ($relative in $pendingDirectories) {
    Write-Output "Windows directory deletion pending: $relative; it disappears when the existing watcher closes its handle."
}
Write-Output 'Release packages, backups, private configurations and source files were retained.'
if ($DeepClean) { Write-Output 'Run the documented dependency setup before the next local build or test.' }
