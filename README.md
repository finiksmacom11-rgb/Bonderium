# Bonderium

## C++ development on Windows

This project uses C++20, the native Windows API, and CMake. Install VS Code with
the recommended Microsoft C/C++ and CMake Tools extensions, plus Python 3,
CMake, and an LLVM-MinGW toolchain (`clang++`/`g++` and `mingw32-make`). Make
sure Python, CMake, and the toolchain's `bin` directory are available on `PATH`.

From a terminal in the project folder, run `python run.py` to configure, build,
and launch the blank `bonderium` window.

In VS Code:

- Press `Ctrl+Shift+B` to configure and build the Debug executable.
- Run **Tasks: Run Task** and choose **CMake: Run** to build and launch the
  blank `bonderium` window.

The window implementation is in `src/main.cpp`. CMake places generated files
in `build/`, which is not source code and should not be committed.