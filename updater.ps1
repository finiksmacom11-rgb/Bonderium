param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath,

    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [switch]$SuppressErrorDialog
)

$ErrorActionPreference = "Stop"
$stagingDirectory = Join-Path $env:TEMP ("Bonderium-update-" + [guid]::NewGuid().ToString("N"))
$replacementPath = "$AppPath.new"
$updaterPath = Join-Path (Split-Path -Parent $AppPath) "updater.ps1"
$replacementUpdaterPath = "$updaterPath.new"
$backupSuffix = "." + [guid]::NewGuid().ToString("N") + ".bak"
$backupPath = "$AppPath$backupSuffix"
$updaterBackupPath = "$updaterPath$backupSuffix"
$appBackedUp = $false
$appInstalled = $false
$updaterBackedUp = $false
$updaterInstalled = $false

try {
    Wait-Process -Id $ProcessId -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stagingDirectory -Force

    $newExecutable = Join-Path $stagingDirectory "Bonderium.exe"
    $newUpdater = Join-Path $stagingDirectory "updater.ps1"
    if (-not (Test-Path -LiteralPath $newExecutable) -or
        -not (Test-Path -LiteralPath $newUpdater) -or
        -not (Test-Path -LiteralPath $AppPath) -or
        -not (Test-Path -LiteralPath $updaterPath)) {
        throw "The release archive does not contain the application files."
    }

    Copy-Item -LiteralPath $newExecutable -Destination $replacementPath -Force
    Copy-Item -LiteralPath $newUpdater -Destination $replacementUpdaterPath -Force
    [System.IO.File]::Move($AppPath, $backupPath)
    $appBackedUp = $true
    [System.IO.File]::Move($updaterPath, $updaterBackupPath)
    $updaterBackedUp = $true
    [System.IO.File]::Move($replacementPath, $AppPath)
    $appInstalled = $true
    [System.IO.File]::Move($replacementUpdaterPath, $updaterPath)
    $updaterInstalled = $true
    Start-Process -FilePath $AppPath
    Remove-Item -LiteralPath $backupPath, $updaterBackupPath -Force -ErrorAction SilentlyContinue
}
catch {
    $updateError = $_.Exception.Message
    $rollbackErrors = @()

    if ($updaterInstalled -and (Test-Path -LiteralPath $updaterPath)) {
        try {
            Remove-Item -LiteralPath $updaterPath -Force
        }
        catch {
            $rollbackErrors += "Could not remove the updated updater: $($_.Exception.Message)"
        }
    }
    if ($updaterBackedUp -and (Test-Path -LiteralPath $updaterBackupPath)) {
        try {
            [System.IO.File]::Move($updaterBackupPath, $updaterPath)
        }
        catch {
            $rollbackErrors += "Could not restore the previous updater: $($_.Exception.Message)"
        }
    }
    if ($appInstalled -and (Test-Path -LiteralPath $AppPath)) {
        try {
            Remove-Item -LiteralPath $AppPath -Force
        }
        catch {
            $rollbackErrors += "Could not remove the updated application: $($_.Exception.Message)"
        }
    }
    if ($appBackedUp -and (Test-Path -LiteralPath $backupPath)) {
        try {
            [System.IO.File]::Move($backupPath, $AppPath)
        }
        catch {
            $rollbackErrors += "Could not restore the previous application: $($_.Exception.Message)"
        }
    }

    $details = $updateError
    if ($rollbackErrors.Count -gt 0) {
        $details += "`n`nRollback warnings:`n" + ($rollbackErrors -join "`n")
    }
    if ($SuppressErrorDialog) {
        throw $details
    }

    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        "The update could not be installed. Start Bonderium and try again.`n`n$details",
        "Bonderium",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
}
finally {
    if (Test-Path -LiteralPath $ArchivePath) {
        Remove-Item -LiteralPath $ArchivePath -Force
    }
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $replacementPath) {
        Remove-Item -LiteralPath $replacementPath -Force
    }
    if (Test-Path -LiteralPath $replacementUpdaterPath) {
        Remove-Item -LiteralPath $replacementUpdaterPath -Force
    }
}
