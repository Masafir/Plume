param(
    [Parameter(Mandatory=$true)][string]$InputVideo,
    [Parameter(Mandatory=$true)][string]$OutputVideo,
    [string]$Ffmpeg,
    [ValidateRange(0,51)][int]$Quality = 20
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $InputVideo).Path
$destination = [IO.Path]::GetFullPath($OutputVideo)
if ($source -eq $destination -or (Test-Path -LiteralPath $destination)) {
    throw 'Choisir un nouveau fichier de sortie : aucun fichier existant ne sera remplacé.'
}
if (-not $Ffmpeg) {
    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($command) { $Ffmpeg = $command.Source }
    else {
        $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
        $Ffmpeg = Get-ChildItem -Path (Join-Path $packages 'Gyan.FFmpeg*') -Directory -ErrorAction SilentlyContinue |
            Get-ChildItem -Filter ffmpeg.exe -File -Recurse | Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $Ffmpeg -or -not (Test-Path -LiteralPath $Ffmpeg)) { throw 'FFmpeg introuvable : fournir -Ffmpeg chemin. Installation requise uniquement pour la préparation.' }
# CUDA decoding keeps decoded pixels on the GPU. NVENC uses the dedicated encoder.
# Deliberately no software fallback: ask the user before choosing another method.
& $Ffmpeg -hide_banner -n -hwaccel cuda -hwaccel_output_format cuda -i $source -map 0:v:0 -an `
    -c:v h264_nvenc -preset p6 -tune hq -rc vbr -cq $Quality -b:v 0 -bf 0 -refs 1 `
    -pix_fmt cuda -movflags +faststart $destination
if ($LASTEXITCODE) { throw 'Préparation GPU échouée. Aucun fallback CPU ; vérifier le pilote et le support du fichier avant de changer de méthode.' }
Get-Item -LiteralPath $destination | Select-Object FullName,Length
