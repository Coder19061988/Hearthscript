<#
.SYNOPSIS
  Збирає Kindlescript і за потреби запускає тести.
.EXAMPLE
  ./build.ps1                               # конфігурація + збірка (RelWithDebInfo)
  ./build.ps1 -Test                         # + усі тести (ctest)
  ./build.ps1 -Test -Filter rain_interrupt  # лише тести, назва яких містить rain_interrupt
  ./build.ps1 -Target kindle_core           # зібрати одну ціль
#>
param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release")] [string]$Config = "RelWithDebInfo",
    [string]$Target = "",
    [switch]$Test,
    [string]$Filter = ""
)
# Native tools report failures via $LASTEXITCODE; "Stop" would turn their stderr output into errors in PowerShell 5.1.
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot

# CMake ships with Visual Studio; use the one from PATH if present.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
$ctest = (Get-Command ctest -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Не знайдено Visual Studio з C++ (MSVC)." }
    $bin = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $cmake = Join-Path $bin "cmake.exe"
    $ctest = Join-Path $bin "ctest.exe"
}

if (-not (Test-Path "build\CMakeCache.txt")) {
    & $cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$buildArgs = @("--build", "build", "--config", $Config, "--parallel")
if ($Target) { $buildArgs += @("--target", $Target) }
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    $testArgs = @("--test-dir", "build", "-C", $Config, "--output-on-failure")
    if ($Filter) { $testArgs += @("-R", $Filter) }
    & $ctest @testArgs
    exit $LASTEXITCODE
}
