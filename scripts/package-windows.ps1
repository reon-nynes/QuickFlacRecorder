param(
    [string]$BuildDirectory = 'build',
    [string]$Destination = 'dist',
    [string]$JuceSource = '',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent $PSScriptRoot
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (!$JuceSource) { $JuceSource = Join-Path $build '_deps/juce-src' }
$binary = Join-Path $build 'QuickFlacRecorder_artefacts/Release/QuickFlacRecorder.exe'
if (!(Test-Path -LiteralPath $binary)) { throw "Missing Release executable: $binary" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools are needed to inspect the executable' }
$toolVersion = (Get-Content -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$dumpbin = Join-Path $installation "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/dumpbin.exe"
$headers = & $dumpbin /headers $binary
if ($LASTEXITCODE -ne 0 -or !($headers -match '8664 machine \(x64\)')) { throw 'Expected a Windows x64 executable' }
$dependencies = & $dumpbin /dependents $binary
if ($LASTEXITCODE -ne 0) { throw 'Dependency inspection failed' }
$imports = @($dependencies | ForEach-Object {
    if ($_ -match '^\s+([\w.-]+\.dll)\s*$') { $Matches[1] }
} | Sort-Object -Unique)
if ($imports.Count -eq 0) { throw 'Dependency inspection did not identify any Windows imports' }
foreach ($dependency in $imports) {
    if ($dependency -match '^(msvcp|msvcr|vcruntime|concrt|ucrtbase|api-ms-win-crt)') {
        throw "Dynamic C/C++ runtime import violates the portable executable requirement: $dependency"
    }
    if ($dependency -notmatch '^api-ms-win-' -and !(Test-Path -LiteralPath (Join-Path "$env:WINDIR/System32" $dependency))) {
        throw "Non-system DLL import must be resolved before packaging: $dependency"
    }
}
$destinationPath = [System.IO.Path]::GetFullPath((Join-Path $repo $Destination))
$package = Join-Path $destinationPath 'QuickFlacRecorder-windows-x64'
New-Item -ItemType Directory -Path $package -Force | Out-Null
& $Python (Join-Path $PSScriptRoot 'package-common.py') $package --juce-source $JuceSource
if ($LASTEXITCODE -ne 0) { throw 'Source/notices packaging failed' }
Copy-Item -LiteralPath $binary -Destination $package
@("Windows x64 Release dependency inspection", "", $headers, "", $dependencies) | Set-Content -LiteralPath (Join-Path $package 'windows-dependencies.txt')
Get-FileHash -LiteralPath $binary -Algorithm SHA256 | Format-List | Out-String | Set-Content -LiteralPath (Join-Path $package 'SHA256.txt')
$archive = Join-Path $destinationPath 'QuickFlacRecorder-windows-x64.zip'
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -Force
Write-Host "Portable Windows preview: $archive"
