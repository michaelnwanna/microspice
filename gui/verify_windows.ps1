param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$required = @(
    'microspice_gui.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'platforms/qwindows.dll', 'msvcp140.dll', 'vcruntime140.dll',
    'examples/voltage_divider.microspice', 'examples/rc_charging.microspice',
    'notices/THIRD-PARTY-NOTICES.txt', 'notices/licenses/LGPL-3.0-only.txt',
    'notices/licenses/GPL-3.0-only.txt', 'notices/licenses/Lucide-LICENSE.txt'
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagePath $file))) {
        throw "Package is missing $file"
    }
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $packagePath 'notices/qt-sbom') -Filter 'qtbase-*.spdx.json' -File)) {
    throw 'Package Qt base SDK SBOM is missing'
}
$version = (Get-Item -LiteralPath (Join-Path $packagePath 'microspice_gui.exe')).VersionInfo
if ($version.ProductName -ne 'microspice' -or [string]::IsNullOrWhiteSpace($version.ProductVersion)) {
    throw 'Executable product/version resource is missing'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$originalPath = $env:PATH
$qtEnvironment = @{}
Get-ChildItem Env: | Where-Object { $_.Name -match '^(QT_|QML)' } | ForEach-Object {
    $qtEnvironment[$_.Name] = $_.Value
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in $qtEnvironment.Keys) { Remove-Item "Env:$name" }
    $process = Start-Process -FilePath (Join-Path $packagePath 'microspice_gui.exe') `
        -ArgumentList '--smoke-test' -WorkingDirectory $outputPath -PassThru `
        -RedirectStandardOutput (Join-Path $outputPath 'stdout.log') `
        -RedirectStandardError (Join-Path $outputPath 'stderr.log')
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        throw 'Deployed Windows smoke test timed out'
    }
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath (Join-Path $outputPath 'stderr.log')
        throw "Deployed Windows smoke test failed: $($process.ExitCode)"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $outputPath 'gui-desktop.png'))) {
        throw 'Deployed Windows smoke test did not produce its window capture'
    }
    Write-Output 'Deployed Windows package passed with Qt SDK paths removed'
}
finally {
    $env:PATH = $originalPath
    foreach ($name in $qtEnvironment.Keys) { Set-Item "Env:$name" $qtEnvironment[$name] }
}