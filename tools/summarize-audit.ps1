param([Parameter(Mandatory=$true)][string]$Log)
$ErrorActionPreference = 'Stop'
$lines = Get-Content -LiteralPath $Log
$start = $null
$records = [System.Collections.Generic.List[object]]::new()
$loops = 0
$currentPeriodic = $null
foreach ($line in $lines) {
    if ($line -match '^\[(\d+)\].*Démarrage') { $start = [double]$Matches[1] }
    if ($line -match 'Loop :') { $loops++ }
    if ($line -match 'MEM stage=') { $currentPeriodic = $null }
    if ($line -match '^\[(\d+)\].*MEM stage=periodic WS=([\d.]+) MiB privateCommit=([\d.]+) MiB peakWS=([\d.]+)') {
        $records.Add([pscustomobject]@{
            Seconds = ([double]$Matches[1] - $start) / 1000
            WorkingSetMiB = [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
            PrivateCommitMiB = [double]::Parse($Matches[3],[Globalization.CultureInfo]::InvariantCulture)
            PeakWorkingSetMiB = [double]::Parse($Matches[4],[Globalization.CultureInfo]::InvariantCulture)
            PrivateWorkingSetMiB = $null
            SharedCommitMiB = $null
            LocalGpuMiB = $null
            NonLocalGpuMiB = $null
        })
        $currentPeriodic = $records[$records.Count-1]
    }
    if ($null -ne $currentPeriodic -and $line -match 'MEM extended privateWS=([\d.]+) MiB sharedCommit=([\d.]+) MiB') {
        $currentPeriodic.PrivateWorkingSetMiB = [double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
        $currentPeriodic.SharedCommitMiB = [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
    }
    if ($null -ne $currentPeriodic -and $line -match 'MEM GPU per-process localUsage=([\d.]+) MiB nonLocalUsage=([\d.]+) MiB') {
        $currentPeriodic.LocalGpuMiB = [double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
        $currentPeriodic.NonLocalGpuMiB = [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)
    }
}
if (-not $records.Count) { throw 'Aucun relevé périodique.' }
$csv = [IO.Path]::ChangeExtension([IO.Path]::GetFullPath($Log),'.csv')
$records | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
$steady = @($records | Where-Object Seconds -ge 30)
$ws = $steady | Measure-Object WorkingSetMiB -Average -Minimum -Maximum
$commit = $steady | Measure-Object PrivateCommitMiB -Average -Minimum -Maximum
$residentPrivate = $steady | Where-Object { $null -ne $_.PrivateWorkingSetMiB } | Measure-Object PrivateWorkingSetMiB -Average -Minimum -Maximum
$sorted = @($steady.WorkingSetMiB | Sort-Object)
$duration = $records[$records.Count-1].Seconds
$early = $steady | Where-Object { $_.Seconds -ge 30 -and $_.Seconds -lt 130 } | Measure-Object WorkingSetMiB -Average -Minimum -Maximum
$late = $steady | Where-Object { $_.Seconds -ge ($duration-100) } | Measure-Object WorkingSetMiB -Average -Minimum -Maximum
[pscustomobject]@{
    PeriodicLastSecond = $duration
    CompletedLoops = $loops
    SamplesAfter30Seconds = $steady.Count
    WorkingSetAverageMiB = [math]::Round($ws.Average,2)
    WorkingSetMedianMiB = $sorted[[int][math]::Floor($sorted.Count/2)]
    WorkingSetMinimumMiB = $ws.Minimum
    WorkingSetSampledMaximumMiB = $ws.Maximum
    PrivateCommitAverageMiB = [math]::Round($commit.Average,2)
    PrivateCommitMinimumMiB = $commit.Minimum
    PrivateCommitMaximumMiB = $commit.Maximum
    PrivateWorkingSetAverageMiB = if ($residentPrivate.Count) { [math]::Round($residentPrivate.Average,2) } else { $null }
    PrivateWorkingSetMinimumMiB = $residentPrivate.Minimum
    PrivateWorkingSetMaximumMiB = $residentPrivate.Maximum
    EarlyWindowAverageMiB = [math]::Round($early.Average,2)
    EarlyWindowMinimumMiB = $early.Minimum
    EarlyWindowMaximumMiB = $early.Maximum
    Last100SecondsAverageMiB = [math]::Round($late.Average,2)
    Last100SecondsMinimumMiB = $late.Minimum
    Last100SecondsMaximumMiB = $late.Maximum
    Csv = $csv
}
