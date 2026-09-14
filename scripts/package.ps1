param([string]$BuildRoot='build/verified',[switch]$SkipSource)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$version=[regex]::Match((Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw),'project\(BridgeVPN VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (!$version) { throw 'Cannot determine application version.' }
$distribution=Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $distribution -Force | Out-Null
$licenseFiles=@(
    @('LICENSE','Application-GPL-3.0.txt'),
    @('.deps/sources/openconnect-9.21/COPYING.LGPL','OpenConnect-LGPL-2.1.txt'),
    @('.deps/sources/openssl-3.5.8/LICENSE.txt','OpenSSL-Apache-2.0.txt'),
    @('.deps/sources/libxml2-2.15.3/Copyright','libxml2-Copyright.txt'),
    @('.deps/sources/zlib-1.3.2/LICENSE','zlib-LICENSE.txt'),
    @('.deps/sources/wintun/LICENSE.txt','Wintun-Prebuilt-License.txt'),
    @('.tools/x64/w64devkit/COPYING.MinGW-w64-runtime.txt','MinGW-w64-runtime.txt')
)
$quickStart=@'
布利杰VPN

1. 完整解压。64 位 Windows 使用 x64 包，32 位 Windows 使用 x86 包。
2. 运行“布利杰VPN.exe”。更新时先退出旧版本窗口。
3. 点击“导入配置”，选择公司提供的 .bvpn 文件；或者填写服务器地址后，在文件类型中选择 CA 证书，导入服务端保存的 ca.pem。
4. 输入账号和密码，点击“连接”。需要时完成 Windows 管理员授权。
5. “记住账号和密码”使用当前 Windows 用户的凭据管理器；取消勾选可清除当前连接的已存凭据。
6. 连接后可断开。最小化进入系统托盘，单击托盘图标还原，右键可断开或退出。关闭窗口会先断开再退出。

默认地址 https://192.168.19.254:4443 是部署示例，请使用管理员确认的实际地址。
必须保留同目录下的 wintun.dll，不要从其他架构程序包替换它。

当前为联调版本，Windows 7 实机及 N1 的完整 VPN 转发尚待验收。应用 EXE 未商业代码签名，Wintun 使用官方签名文件。
请向提供本程序的管理员索取同版本源码包，相关许可证见 licenses 与 THIRD-PARTY-NOTICES.md。

English
Extract the whole archive. Import your company's .bvpn profile, or set the server address and import ca.pem using the CA certificate file filter.
Enter your username and password and connect. Remembered credentials are stored in Windows Credential Manager for the current Windows user.
Minimize to the notification area; click its icon to restore, or right-click to disconnect/exit. Closing the window disconnects before exiting.
Use the matching x86/x64 package. Administrator privileges are needed to configure the tunnel. This build still requires Windows 7 and real VPN deployment validation.
'@
foreach($arch in @('x64','x86')) {
    $name="BulijieVPN-$version-windows-$arch"
    $folder=Join-Path $distribution $name
    $licenses=Join-Path $folder 'licenses'
    New-Item -ItemType Directory -Path $folder,$licenses -Force | Out-Null
    $build=Join-Path (Join-Path $projectRoot $BuildRoot) $arch
    $sourceExe=Join-Path $build '布利杰VPN.exe'
    if(!(Test-Path -LiteralPath $sourceExe)) { throw "Missing build: $sourceExe" }
    $versionInfo=(Get-Item -LiteralPath $sourceExe).VersionInfo.ProductVersion
    if($versionInfo -ne $version) { throw "Unexpected product version: $versionInfo" }
    Copy-Item -LiteralPath $sourceExe -Destination $folder -Force
    Copy-Item -LiteralPath (Join-Path $build 'wintun.dll') -Destination $folder -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'config/BridgeVPN.ini') -Destination $folder -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD-PARTY-NOTICES.md') -Destination $folder -Force
    foreach($license in $licenseFiles) { Copy-Item -LiteralPath (Join-Path $projectRoot $license[0]) -Destination (Join-Path $licenses $license[1]) -Force }
    [IO.File]::WriteAllText((Join-Path $folder '使用说明.txt'),$quickStart,[Text.UTF8Encoding]::new($true))
    $exe=Join-Path $folder '布利杰VPN.exe'
    & (Join-Path $projectRoot ".tools/$arch/w64devkit/bin/strip.exe") --strip-all $exe
    if($LASTEXITCODE) { throw "Strip failed for $arch" }
    python (Join-Path $PSScriptRoot 'audit-pe.py') $exe --output (Join-Path $projectRoot "test-results/release-imports-$arch.json")
    if($LASTEXITCODE) { throw "Import audit failed for $arch" }
    $zip=Join-Path $distribution "$name.zip"
    Compress-Archive -Path (Join-Path $folder '*') -DestinationPath $zip -CompressionLevel Optimal -Force
    Get-Item -LiteralPath $exe,$zip | Select-Object FullName,Length |ConvertTo-Json -Compress
}
if(!$SkipSource) {
    python (Join-Path $PSScriptRoot 'package-source.py')
    if($LASTEXITCODE) { throw 'Source package creation failed' }
}
$hashes=Get-ChildItem -LiteralPath $distribution -Filter '*.zip' -File | ForEach-Object {
    $hash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
}
[IO.File]::WriteAllLines((Join-Path $distribution 'SHA256SUMS.txt'),$hashes,[Text.Encoding]::ASCII)
