$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Installer Visual Studio 2022/2026 Build Tools : C++ Desktop, CMake et Windows SDK 10.0.26100 récent (attribut MF_READWRITE_USE_ONLY_HARDWARE_TRANSFORMS requis).' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ x64 introuvable.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Composant CMake de Visual Studio manquant.' }
$sdkInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdkVersion = Get-ChildItem -LiteralPath $sdkInclude -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } | Sort-Object { [Version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
& $cmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -A x64 "-DCMAKE_SYSTEM_VERSION=$sdkVersion"
if ($LASTEXITCODE) { throw 'Configuration CMake échouée.' }
& $cmake --build (Join-Path $PSScriptRoot 'build') --config Release
if ($LASTEXITCODE) { throw 'Compilation échouée.' }
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'dist') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build\Release\PlumeWallpaper.exe') -Destination (Join-Path $PSScriptRoot 'dist\PlumeWallpaper.exe')
New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'dist\V0') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build\Release\PlumeV0.exe') -Destination (Join-Path $PSScriptRoot 'dist\V0\PlumeV0.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'V0-README.md') -Destination (Join-Path $PSScriptRoot 'dist\V0\README.md')
