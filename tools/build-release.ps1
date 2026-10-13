param(
    [switch]$SkipBuild,
    [switch]$RequireSignature,
    [string]$IsccPath,
    [string]$SignToolCommand
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $projectRoot 'build.ps1') }
$binary = Join-Path $projectRoot 'build\Release\PlumeV0.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Compiler PlumeV0 en Release avant de créer la distribution.' }
$version = (Get-Item -LiteralPath $binary).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+-alpha$') { throw 'Version de produit absente ou inattendue dans le PE.' }
$signature = Get-AuthenticodeSignature -LiteralPath $binary
if ($RequireSignature -and $signature.Status -ne 'Valid') { throw 'Une signature Authenticode reconnue est nécessaire avec -RequireSignature.' }
if (-not $IsccPath) {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $IsccPath) { throw 'Inno Setup 6 manquant ; fournir -IsccPath ou installer JRSoftware.InnoSetup.' }
$releaseDir = Join-Path $projectRoot "dist\release\$version"
$stageRoot = Join-Path $projectRoot ('dist\.release-stage\' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stageRoot 'portable'
$sources = Join-Path $stageRoot 'source'
New-Item -ItemType Directory -Force $releaseDir,$payload,$sources | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $payload 'Plume.exe')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\INSTALLATION.md') -Destination (Join-Path $payload 'README.md')
foreach ($file in @('LICENSE','THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $payload $file)
}
Copy-Item -LiteralPath (Join-Path $payload 'Plume.exe') -Destination (Join-Path $releaseDir 'Plume.exe')
Copy-Item -LiteralPath (Join-Path $payload 'README.md') -Destination (Join-Path $releaseDir 'README.md')
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath (Join-Path $releaseDir "Plume-$version-Portable.zip") -Force
# Explicit source allowlist excludes videos, logs, config, build outputs, Git and tool installations.
foreach ($folder in @('src','assets','packaging','tests')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $folder) -Destination (Join-Path $sources $folder) -Recurse
}
New-Item -ItemType Directory -Force (Join-Path $sources 'tools'),(Join-Path $sources 'docs') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tools') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $sources 'tools')
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\INSTALLATION.md') -Destination (Join-Path $sources 'docs\INSTALLATION.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\DISTRIBUTION.md') -Destination (Join-Path $sources 'docs\DISTRIBUTION.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\RELEASE-VALIDATION.md') -Destination (Join-Path $sources 'docs\RELEASE-VALIDATION.md')
foreach ($file in @('CMakeLists.txt','app.manifest','build.ps1','.clang-format','.gitignore','LICENSE','THIRD-PARTY-NOTICES.md','README.md','DEVELOPMENT.md','ARCHITECTURE.md','V0-README.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $sources $file)
}
Compress-Archive -Path (Join-Path $sources '*') -DestinationPath (Join-Path $releaseDir "Plume-$version-Source.zip") -Force
$compilerArguments = @("/DSourceRoot=$projectRoot", "/DPayloadDir=$payload", "/DAppVersion=$version", "/DOutputDir=$releaseDir")
if ($SignToolCommand) { $compilerArguments += @('/DSignedBuild', "/SPlumeSigning=$SignToolCommand") }
$compilerArguments += (Join-Path $projectRoot 'packaging\Plume.iss')
& $IsccPath @compilerArguments
if ($LASTEXITCODE) { throw 'Compilation du setup échouée.' }
$setup = Join-Path $releaseDir "Plume-$version-Setup.exe"
$setupSignature = Get-AuthenticodeSignature -LiteralPath $setup
if ($RequireSignature -and $setupSignature.Status -ne 'Valid') { throw 'Le setup doit aussi être signé. Signer le setup, puis générer ses empreintes définitives.' }
$artifacts = Get-ChildItem -LiteralPath $releaseDir -File | Where-Object {$_.Extension -in '.exe','.zip'} | Sort-Object Name
$entries = @($artifacts | ForEach-Object {
    [ordered]@{file=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$entries | ForEach-Object { $_.sha256 + '  ' + $_.file } | Set-Content -LiteralPath (Join-Path $releaseDir 'SHA256SUMS.txt') -Encoding utf8
[ordered]@{
    product='Plume';version=$version;architecture='x64';channel='alpha';license='MIT'
    generatedUtc=[DateTime]::UtcNow.ToString('o')
    applicationSignature=$signature.Status.ToString();setupSignature=$setupSignature.Status.ToString()
    ffmpegBundled=$false;artifacts=$entries
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $releaseDir 'RELEASE.json') -Encoding utf8
Write-Output $releaseDir

