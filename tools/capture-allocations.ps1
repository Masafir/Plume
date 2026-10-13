param(
    [Parameter(Mandatory=$true)][string]$Video,
    [int]$Seconds = 20
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$wpr = Join-Path $env:SystemRoot 'System32\wpr.exe'
$xperf = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$status = (& $wpr -status 2>&1) -join "`n"
if ($status -notmatch 'WPR is not recording') { throw 'Une session WPR existe déjà : ne pas la modifier.' }
if (-not (Test-Path -LiteralPath $xperf)) { throw 'Windows Performance Toolkit absent.' }
$output = Join-Path $root 'audit'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$trace = Join-Path $output 'allocations.etl'
& $wpr -start VirtualAllocation -filemode
if ($LASTEXITCODE) { throw 'Capture VirtualAllocation refusée.' }
try {
    $videoQuoted = '"' + $Video + '"'
    $reportQuoted = '"' + (Join-Path $output 'trace-run.log') + '"'
    $traceProcess = Start-Process -FilePath (Join-Path $root 'dist\PlumeWallpaper.exe') -ArgumentList "$videoQuoted --strict --primary --test-seconds $Seconds --report $reportQuoted" -WindowStyle Hidden -PassThru
    $tracePid = $traceProcess.Id
    Write-Output "Trace PID=$tracePid"
    $traceProcess.WaitForExit()
    Write-Output "Trace process ExitCode=$($traceProcess.ExitCode)"
} finally {
    & $wpr -stop $trace 'PlumeWallpaper native GPU allocation audit'
    if ($LASTEXITCODE) { throw 'Impossible de terminer la capture WPR ; vérifier wpr -status.' }
}
$savedSymbols = $env:_NT_SYMBOL_PATH
$savedCache = $env:_NT_SYMCACHE_PATH
try {
    $env:_NT_SYMBOL_PATH = 'srv*' + (Join-Path $output 'symbols') + '*https://msdl.microsoft.com/download/symbols'
    $env:_NT_SYMCACHE_PATH = Join-Path $output 'symcache'
    & $xperf -i $trace -symbols -o (Join-Path $output 'allocation-stacks.txt') -a virtualalloc -pid $tracePid -stacks sc -top 20
    if ($LASTEXITCODE) { throw 'Analyse des piles échouée.' }
    & $xperf -i $trace -symbols -o (Join-Path $output 'allocation-images.txt') -a virtualalloc -pid $tracePid -images sc -top 20
    if ($LASTEXITCODE) { throw 'Analyse par image échouée.' }
} finally {
    $env:_NT_SYMBOL_PATH = $savedSymbols
    $env:_NT_SYMCACHE_PATH = $savedCache
}
Get-Content -LiteralPath (Join-Path $output 'allocation-images.txt') -TotalCount 70
