param(
    [Parameter(Mandatory = $true)][string]$IsccPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$PackageVersion = "0.0.5-preview.1",
    [string]$PackageFileVersion = "0.0.5.0"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$output = [IO.Path]::GetFullPath($OutputDirectory)
$publish = Join-Path $repo "publish"
$hostDirectory = Get-ChildItem $repo -Directory | Where-Object { Test-Path (Join-Path $_.FullName "app.manifest") } | Select-Object -First 1
$hostName = $hostDirectory.Name
$libraryDirectory = Get-ChildItem $repo -Directory | Where-Object { Test-Path (Join-Path $_.FullName "LibreHardwareMonitor-master") } | Select-Object -First 1
$dotnet = (Get-Command dotnet -ErrorAction Stop).Source
if (!(Test-Path $IsccPath -PathType Leaf)) { throw "ISCC.exe not found: $IsccPath" }
if (Test-Path $output) { throw "Output directory must be new to avoid stale files: $output" }
if ($output.StartsWith($publish.TrimEnd("\") + "\", [StringComparison]::OrdinalIgnoreCase) -or $output -eq $publish) { throw "Output must be outside publish" }
if ($PackageVersion -notmatch "^\d+\.\d+\.\d+-preview\.\d+$" -or $PackageFileVersion -notmatch "^\d+\.\d+\.\d+\.\d+$") { throw "Expected preview SemVer and four-part file version" }
New-Item -ItemType Directory -Path $output | Out-Null
function Run-Dotnet([string[]]$Arguments) {
    & $dotnet @Arguments
    if ($LASTEXITCODE -ne 0) { throw "dotnet failed with exit code $LASTEXITCODE" }
}
# Existing upstream order is significant: first publish self-contained FurMark, then the host.
$payload = Join-Path $output "BibaInspection-$PackageVersion-win-x64"
Run-Dotnet @("publish", (Join-Path $repo "FurMark/FurMark.csproj"), "-p:PublishProfile=FolderProfile", "-p:GeneratePackageOnBuild=false", "--nologo")
Run-Dotnet @("publish", (Join-Path $hostDirectory.FullName "$hostName.csproj"), "-p:PublishProfile=FolderProfile", "-p:GeneratePackageOnBuild=false", "-p:Version=$PackageVersion", "-p:PublishDir=$payload/", "--nologo")
# Include redistribution notices and the exact source revision with both package forms.
Copy-Item (Join-Path $repo "LICENSE") (Join-Path $payload "LICENSE.txt")
Copy-Item (Join-Path $repo "AUDIO-LICENSE.md") (Join-Path $payload "AUDIO-LICENSE.md")
$licenses = Join-Path $payload "licenses"
New-Item -ItemType Directory $licenses | Out-Null
Copy-Item (Join-Path $libraryDirectory.FullName "LibreHardwareMonitor-master/LICENSE") (Join-Path $licenses "LibreHardwareMonitor-MPL-2.0.txt")
Copy-Item (Join-Path $libraryDirectory.FullName "LibreHardwareMonitor-master/LibreHardwareMonitorLib/Resources/PawnIo/COPYING") (Join-Path $licenses "PawnIO-COPYING.txt")
$required = @("$hostName.exe", "coreclr.dll", "hostfxr.dll", "YanJi.PluginSdk.dll", "Tools/furmark/FurMark.exe", "Tools/furmark/coreclr.dll", "Tools/prime95/prime95.exe", "Tools/pawnio/PawnIO_setup.exe", "plugins/disk/CdmHost/CdmHost.exe", "plugins/disk/CdmHost/CdmResource/diskspd/diskspd64.exe")
foreach ($relative in $required) { if (!(Test-Path (Join-Path $payload $relative) -PathType Leaf)) { throw "Missing package payload: $relative" } }
$pluginIds = @("config", "keyboard", "screen", "av", "stress", "disk")
$actualPluginIds = @(Get-ChildItem (Join-Path $payload "plugins") -Directory | Select-Object -ExpandProperty Name)
if (@(Compare-Object ($pluginIds | Sort-Object) ($actualPluginIds | Sort-Object)).Count -gt 0) { throw "Unexpected or missing plugin directory" }
foreach ($id in $pluginIds) {
    $plugin = Get-Content (Join-Path $payload "plugins/$id/plugin.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($plugin.id -ne $id -or !(Test-Path (Join-Path $payload "plugins/$id/$($plugin.entryAssembly)"))) { throw "Invalid plugin: $id" }
}
foreach ($relative in @("$hostName.runtimeconfig.json", "Tools/furmark/FurMark.runtimeconfig.json")) {
    $runtime = (Get-Content (Join-Path $payload $relative) -Raw -Encoding UTF8 | ConvertFrom-Json).runtimeOptions
    if (!$runtime.includedFrameworks -or $runtime.framework -or $runtime.frameworks) { throw "Payload is not self-contained: $relative" }
}
$revision = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Cannot resolve source revision" }
$dirty = @(& git -C $repo status --porcelain)
if ($dirty.Count -gt 0) { throw "Commit source changes before producing a traceable release" }
[IO.File]::WriteAllText((Join-Path $payload "preview-mode"), "")
$manifest = [ordered]@{ version = $PackageVersion; createdAt = [DateTimeOffset]::Now.ToString("o"); source = "https://github.com/beifeng08/BibaYanji"; sourceRevision = $revision; sourceDirty = ($dirty.Count -gt 0); plugins = $pluginIds; platform = "win-x64"; runtime = "self-contained .NET 10"; batteryAcceptance = "user-owned"; distribution = "local preview, not public release" }
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $payload "release-manifest.json") -Encoding UTF8
@("BibaInspection $PackageVersion / win-x64", "Run: extract the complete directory and start the main EXE as administrator.", "Six plugins and .NET runtime are included. Do not move the EXE alone.", "This is a local acceptance preview. Battery edge-case acceptance belongs to the user.", "The preview uses its own AppId/directory and settings/plugin queue. Disk result storage remains shared; uninstall preserves user data.", "Monitor acceptance does not imply a completed stress, driver reinstall or fresh-machine test.", "Source: https://github.com/beifeng08/BibaYanji (see release-manifest.json)") | Set-Content (Join-Path $payload "START-HERE.txt") -Encoding UTF8
& $IsccPath "/Qp" "/DBlockThreads=4" "/DPreview=1" "/DPackageVersion=$PackageVersion" "/DPackageFileVersion=$PackageFileVersion" "/DPublishSource=$payload" "/O$output" (Join-Path $PSScriptRoot "setup.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed: $LASTEXITCODE" }
$zip = Join-Path $output "BibaInspection-$PackageVersion-win-x64-portable.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($payload, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
Get-ChildItem $output -File | Where-Object { $_.Extension -in @(".zip", ".exe") } | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } | Set-Content (Join-Path $output "SHA256SUMS.txt") -Encoding ASCII
Get-ChildItem $output -File | Select-Object Name,Length
