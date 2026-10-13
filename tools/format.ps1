param([switch]$Check)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$bundledFormatter = Join-Path $PSScriptRoot '.formatter\clang_format\data\bin\clang-format.exe'
$formatter = if (Test-Path -LiteralPath $bundledFormatter) { $bundledFormatter } else { (Get-Command clang-format -ErrorAction Stop).Source }
Push-Location $projectRoot
try {
    $sources = rg --files src tests tools -g '*.cpp' -g '*.h' -g '!tools/.formatter/**'
    if ($LASTEXITCODE) { throw 'Liste des sources introuvable (rg requis).' }
    if ($Check) { & $formatter --dry-run --Werror $sources }
    else { & $formatter -i $sources }
    if ($LASTEXITCODE) { throw 'Mise en forme C++ non conforme.' }
} finally { Pop-Location }
