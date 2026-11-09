$ErrorActionPreference = "Stop"

$testDirectory = Join-Path $env:TEMP (
    "Bonderium-updater-test-" + [guid]::NewGuid().ToString("N"))
$installDirectory = Join-Path $testDirectory "install"
$packageDirectory = Join-Path $testDirectory "package"
$archivePath = Join-Path $testDirectory "update.zip"
$updaterPath = Join-Path $installDirectory "updater.ps1"
$appPath = Join-Path $installDirectory "Bonderium.exe"
$sourceUpdater = Join-Path $PSScriptRoot "..\updater.ps1"

try {
    New-Item -ItemType Directory -Path $installDirectory, $packageDirectory -Force |
        Out-Null
    Set-Content -LiteralPath $appPath -Value "old executable" -NoNewline
    Set-Content -LiteralPath $updaterPath -Value "old updater" -NoNewline
    Set-Content -LiteralPath (Join-Path $packageDirectory "Bonderium.exe") `
        -Value "invalid executable" -NoNewline
    Set-Content -LiteralPath (Join-Path $packageDirectory "updater.ps1") `
        -Value "new updater" -NoNewline
    Compress-Archive -Path (Join-Path $packageDirectory "*") `
        -DestinationPath $archivePath

    $failureObserved = $false
    try {
        & $sourceUpdater -ArchivePath $archivePath -AppPath $appPath `
            -ProcessId 2147483647 -SuppressErrorDialog
    }
    catch {
        $failureObserved = $true
    }

    if (-not $failureObserved) {
        throw "The updater did not report the expected invalid-executable failure."
    }
    if ((Get-Content -LiteralPath $appPath -Raw) -ne "old executable") {
        throw "The updater did not restore the previous executable."
    }
    if ((Get-Content -LiteralPath $updaterPath -Raw) -ne "old updater") {
        throw "The updater did not restore the previous updater script."
    }
    if (Test-Path -LiteralPath "$appPath.new") {
        throw "The updater left an executable replacement file behind."
    }
    if (Test-Path -LiteralPath "$updaterPath.new") {
        throw "The updater left a script replacement file behind."
    }
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        Remove-Item -LiteralPath $testDirectory -Recurse -Force
    }
}

Write-Output "Updater integration tests passed."
