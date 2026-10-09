import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent
BUILD_DIR = PROJECT_DIR / "build"


def build_project():
    if sys.platform != "win32":
        raise SystemExit("Bonderium currently runs on Windows only.")
    if shutil.which("cmake") is None:
        raise SystemExit("CMake was not found on PATH. Install CMake and the MinGW toolchain.")

    subprocess.run(
        ["cmake", "-S", str(PROJECT_DIR), "-B", str(BUILD_DIR), "-G", "MinGW Makefiles"],
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--config", "Debug"],
        check=True,
    )
    return BUILD_DIR / "Bonderium.exe"


def main():
    executable = build_project()
    subprocess.run([str(executable)], cwd=PROJECT_DIR, check=True)


if __name__ == "__main__":
    main()
