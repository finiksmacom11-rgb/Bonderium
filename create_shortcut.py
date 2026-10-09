import shutil
import subprocess
import sys

from run import build_project


def main():
    if sys.platform != "win32":
        raise SystemExit("Creating a desktop shortcut is supported on Windows only.")

    powershell = shutil.which("powershell")
    if powershell is None:
        raise SystemExit("Windows PowerShell was not found.")

    executable = build_project()
    target = str(executable).replace("'", "''")
    script = (
        "$ErrorActionPreference = 'Stop'; "
        "$target = '" + target + "'; "
        "$desktop = [Environment]::GetFolderPath('Desktop'); "
        "$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut("
        "[IO.Path]::Combine($desktop, 'bonderium.lnk')); "
        "$shortcut.TargetPath = $target; "
        "$shortcut.WorkingDirectory = [IO.Path]::GetDirectoryName($target); "
        "$shortcut.Description = 'Open Bonderium'; "
        "$shortcut.IconLocation = $target + ',0'; "
        "$shortcut.Save()"
    )
    subprocess.run(
        [powershell, "-NoProfile", "-NonInteractive", "-Command", script],
        check=True,
    )
    print("Created the bonderium shortcut on your desktop.")


if __name__ == "__main__":
    main()
