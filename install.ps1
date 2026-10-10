$ErrorActionPreference = "Stop"

$releaseUrl = "https://github.com/finiksmacom11-rgb/Bonderium/releases/latest/download/Bonderium-windows-x64.zip"
$installDirectory = Join-Path $env:LOCALAPPDATA "Programs\Bonderium"
$workDirectory = Join-Path $env:TEMP ("Bonderium-install-" + [guid]::NewGuid().ToString("N"))
$packageDirectory = $PSScriptRoot

try {
    if (Get-Process -Name "Bonderium" -ErrorAction SilentlyContinue) {
        throw "Close Bonderium and run the installer again."
    }

    $executable = Join-Path $packageDirectory "Bonderium.exe"
    $updater = Join-Path $packageDirectory "updater.ps1"
    if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $updater)) {
        $archivePath = Join-Path $workDirectory "Bonderium-windows-x64.zip"
        $stagingDirectory = Join-Path $workDirectory "package"
        New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
        Write-Host "Downloading the latest Bonderium release..."
        Invoke-WebRequest -Uri $releaseUrl -OutFile $archivePath -UseBasicParsing

        if ((Get-Item -LiteralPath $archivePath).Length -eq 0) {
            throw "The downloaded archive is empty."
        }

        Expand-Archive -LiteralPath $archivePath -DestinationPath $stagingDirectory -Force
        $packageDirectory = $stagingDirectory
        $executable = Join-Path $packageDirectory "Bonderium.exe"
        $updater = Join-Path $packageDirectory "updater.ps1"
        if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $updater)) {
            throw "The release archive does not contain the application files."
        }
    }

    New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
    Copy-Item -LiteralPath $executable -Destination (Join-Path $installDirectory "Bonderium.exe") -Force
    Copy-Item -LiteralPath $updater -Destination (Join-Path $installDirectory "updater.ps1") -Force

    $desktop = [Environment]::GetFolderPath("Desktop")
    $shortcutPath = Join-Path $desktop "bonderium.lnk"
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $installDirectory "Bonderium.exe"
    $shortcut.WorkingDirectory = $installDirectory
    $shortcut.Description = "Bonderium"
    $shortcut.IconLocation = $shortcut.TargetPath + ",0"
    $shortcut.Save()

    Write-Host "Bonderium is installed. Launch it using the desktop shortcut."
}
finally {
    if (Test-Path -LiteralPath $workDirectory) {
        Remove-Item -LiteralPath $workDirectory -Recurse -Force
    }
}
