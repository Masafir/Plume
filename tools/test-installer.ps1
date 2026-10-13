param([string]$Version='0.2.0-alpha')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$testDirectory=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist\installer-smoke'))
$allowed=[IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))+[IO.Path]::DirectorySeparatorChar
if(-not $testDirectory.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Chemin de test hors du projet.'}
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{AF0719D3-CE51-4414-A419-4D7D93D796CC}_is1'
if(Test-Path -LiteralPath $key){throw 'Une installation Plume existe déjà : ne pas écraser son inscription pour le test.'}
$setup=Join-Path $projectRoot "dist\release\$Version\Plume-$Version-Setup.exe"
if(-not(Test-Path -LiteralPath $setup)){throw 'Setup non construit.'}
$running=Join-Path $projectRoot 'build\Release\PlumeV0.exe'
$quit=Start-Process -FilePath $running -ArgumentList '--quit' -WindowStyle Hidden -Wait -PassThru
if($quit.ExitCode){throw 'Plume ne peut pas être arrêté proprement.'}
$installLog=Join-Path $projectRoot 'installer-install.log'
$uninstallLog=Join-Path $projectRoot 'installer-uninstall.log'
$app=Join-Path $testDirectory 'Plume.exe'
$uninstaller=Join-Path $testDirectory 'unins000.exe'
try {
 $arguments='/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOICONS /TASKS="" /DIR="'+$testDirectory+'" /LOG="'+$installLog+'"'
 $install=Start-Process -FilePath $setup -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
 if($install.ExitCode){throw "Installation échouée : $($install.ExitCode)"}
 if(-not(Test-Path -LiteralPath $app) -or -not(Test-Path -LiteralPath $uninstaller)){throw 'Programme ou désinstalleur absent.'}
 if((Get-FileHash -LiteralPath $app).Hash -ne (Get-FileHash -LiteralPath (Join-Path $projectRoot "dist\release\$Version\Plume.exe")).Hash){throw 'Binaire installé différent.'}
 if(-not(Test-Path -LiteralPath $key)){throw 'Inscription de désinstallation absente.'}
 $report=Join-Path $projectRoot 'installer-app.log'
 $run=Start-Process -FilePath $app -ArgumentList ('--test-seconds 8 --ui-smoke-test --report "'+$report+'"') -WindowStyle Hidden -Wait -PassThru
 if($run.ExitCode){throw "Lecture installée échouée : $($run.ExitCode)"}
 $results=Get-Content -LiteralPath $report
 if(-not($results -match 'UI smoke')){throw 'Test interface non exécuté.'}
 if($results -match 'ÉCHEC'){throw 'Erreur de lecture dans le rapport.'}
 $uninstall=Start-Process -FilePath $uninstaller -ArgumentList ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+$uninstallLog+'"') -WindowStyle Hidden -Wait -PassThru
 if($uninstall.ExitCode){throw "Désinstallation échouée : $($uninstall.ExitCode)"}
 if(Test-Path -LiteralPath $app){throw 'Binaire encore présent après désinstallation.'}
 if(Test-Path -LiteralPath $key){throw 'Inscription de désinstallation encore présente.'}
 'Installation, intégrité du binaire, interface/lecture et désinstallation : réussies. FFmpeg et autostart non modifiés.'
} finally {
 # Resume the workspace version so the user's wallpaper is not left stopped.
 Copy-Item -LiteralPath $running -Destination (Join-Path $projectRoot 'dist\V0\PlumeV0.exe')
 Start-Process -FilePath (Join-Path $projectRoot 'dist\V0\PlumeV0.exe') | Out-Null
}
