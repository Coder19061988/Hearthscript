<#
.SYNOPSIS
  Збирає Kindlescript і запускає гру в Godot.
.EXAMPLE
  ./run.ps1             # зібрати й запустити гру
  ./run.ps1 -Editor     # зібрати й відкрити проєкт у редакторі Godot
  ./run.ps1 -NoBuild    # запустити без збирання
#>
param(
    [switch]$Editor,
    [switch]$NoBuild,
    [string]$Godot = "D:\Godot4\Godot_v4.6.2-stable_win64.exe"
)
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot

if (-not (Test-Path $Godot)) {
    $found = Get-Command godot -ErrorAction SilentlyContinue
    if (-not $found) { throw "Не знайдено Godot: $Godot (вкажіть шлях: ./run.ps1 -Godot <шлях до Godot.exe>)" }
    $Godot = $found.Source
}

if (-not $NoBuild) {
    & "$PSScriptRoot\build.ps1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# First run of a fresh copy: import the project (registers the extension) before opening it.
# Godot 4.6.2 + godot-cpp 10.0.0 may crash on exit after the very first import; a second import fixes it.
if (-not (Test-Path "godot\.godot\extension_list.cfg")) {
    $console = $Godot -replace "\.exe$", "_console.exe"
    if (-not (Test-Path $console)) { $console = $Godot }
    Write-Host "Перший запуск: імпорт проєкту Godot..."
    & $console --headless --path godot --import 2>&1 | Out-Null
    if (-not (Test-Path "godot\.godot\extension_list.cfg")) { & $console --headless --path godot --import 2>&1 | Out-Null }
}

$arguments = @("--path", "godot")
if ($Editor) { $arguments += "-e" }
Start-Process -FilePath $Godot -ArgumentList $arguments -WorkingDirectory $PSScriptRoot
