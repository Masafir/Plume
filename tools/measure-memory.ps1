param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [int]$Seconds = 120,
    [int]$WarmupSeconds = 30,
    [string]$Output = 'memory.csv'
)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 2 -or $WarmupSeconds -lt 0) { throw 'Durées invalides.' }
Start-Sleep -Seconds $WarmupSeconds
$records = [System.Collections.Generic.List[object]]::new()
$previous = Get-Process -Id $ProcessId
$previousCpu = $previous.TotalProcessorTime.TotalSeconds
$previousTime = [DateTime]::UtcNow
for ($i = 0; $i -lt $Seconds; $i++) {
    Start-Sleep -Seconds 1
    $p = Get-Process -Id $ProcessId
    $time = [DateTime]::UtcNow
    $cpu = $p.TotalProcessorTime.TotalSeconds
    $records.Add([pscustomobject]@{
        Timestamp = $time.ToString('o')
        WorkingSetMiB = [Math]::Round($p.WorkingSet64 / 1MB, 2)
        PrivateBytesMiB = [Math]::Round($p.PrivateMemorySize64 / 1MB, 2)
        CpuOneCorePercent = [Math]::Round(100 * ($cpu - $previousCpu) / ($time - $previousTime).TotalSeconds, 2)
    })
    $previousCpu = $cpu
    $previousTime = $time
}
$records | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8
$working = $records | Measure-Object WorkingSetMiB -Average -Maximum -Minimum
$private = $records | Measure-Object PrivateBytesMiB -Average -Maximum
$cpuStats = $records | Measure-Object CpuOneCorePercent -Average -Maximum
[pscustomobject]@{
    WorkingSetAverageMiB = [Math]::Round($working.Average,2)
    WorkingSetMaxMiB = $working.Maximum
    PrivateBytesAverageMiB = [Math]::Round($private.Average,2)
    PrivateBytesMaxMiB = $private.Maximum
    CpuOneCoreAveragePercent = [Math]::Round($cpuStats.Average,2)
    TargetWorkingSetUnder50MiB = $working.Maximum -lt 50
    Csv = [IO.Path]::GetFullPath($Output)
}
