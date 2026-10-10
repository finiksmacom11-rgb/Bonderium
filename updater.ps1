param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath,

    [Parameter(Mandatory = $true)]
    [string]$AppPath,

    [Parameter(Mandatory = $true)]
    [int]$ProcessId
)

$ErrorActionPreference = "Stop"
$stagingDirectory = Join-Path $env:TEMP ("Bonderium-update-" + [guid]::NewGuid().ToString("N"))
$replacementPath = "$AppPath.new"
$backupPath = "$AppPath.bak"
$updaterPath = Join-Path (Split-Path -Parent $AppPath) "updater.ps1"
$appReplaced = $false

try {
    Wait-Process -Id $ProcessId -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stagingDirectory -Force

    $newExecutable = Join-Path $stagingDirectory "Bonderium.exe"
    $newUpdater = Join-Path $stagingDirectory "updater.ps1"
    if (-not (Test-Path -LiteralPath $newExecutable) -or -not (Test-Path -LiteralPath $newUpdater)) {
        throw "The release archive does not contain the application files."
    }

    Copy-Item -LiteralPath $newExecutable -Destination $replacementPath -Force
    if (Test-Path -LiteralPath $backupPath) {
        Remove-Item -LiteralPath $backupPath -Force
    }
    [System.IO.File]::Move($AppPath, $backupPath)
    try {
        [System.IO.File]::Move($replacementPath, $AppPath)
        $appReplaced = $true
        Copy-Item -LiteralPath $newUpdater -Destination $updaterPath -Force
    }
    catch {
        if (Test-Path -LiteralPath $AppPath) {
            Remove-Item -LiteralPath $AppPath -Force
        }
        [System.IO.File]::Move($backupPath, $AppPath)
        throw
    }

    Remove-Item -LiteralPath $backupPath -Force
    Start-Process -FilePath $AppPath
}
catch {
    if (-not $appReplaced -and (Test-Path -LiteralPath $replacementPath)) {
        Remove-Item -LiteralPath $replacementPath -Force
    }
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        "The update could not be installed. Start Bonderium and try again.`n`n$($_.Exception.Message)",
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
}
