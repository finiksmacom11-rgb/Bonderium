# Bonderium

## C++ development on Windows

This project uses C++20 and CMake. Install VS Code with the recommended
Microsoft C/C++ and CMake Tools extensions, plus CMake and an LLVM-MinGW
toolchain (`clang++`/`g++` and `mingw32-make`). Make sure CMake and the
toolchain's `bin` directory are available on `PATH`.

Open this folder in VS Code, then:

- Press `Ctrl+Shift+B` to configure and build the Debug executable.
- Run **Tasks: Run Task** and choose **CMake: Run** to build and launch it.

The starter program is in `src/main.cpp`. CMake places generated files in
`build/`, which is not source code and should not be committed.