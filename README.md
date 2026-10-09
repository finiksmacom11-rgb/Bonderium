# Bonderium

## Разработка на C++ в Windows

Проект использует C++20, Windows API и CMake. Для разработки установите:

- VS Code с расширениями Microsoft C/C++ и CMake Tools;
- Python 3;
- CMake;
- компилятор LLVM-MinGW (`clang++` или `g++`) и `mingw32-make`.

Добавьте Python, CMake и папку `bin` LLVM-MinGW в переменную `PATH`.

## Запуск

Откройте папку проекта в VS Code или терминале.

Чтобы собрать проект и сразу запустить пустое окно `bonderium`, выполните:

```powershell
python run.py
```

Чтобы собрать приложение и создать ярлык **bonderium** на рабочем столе,
выполните:

```powershell
python create_shortcut.py
```

После этого запускайте приложение с рабочего стола через созданный ярлык.
Если вы переместили папку проекта, повторно выполните команду, чтобы обновить
путь в ярлыке:

```powershell
python create_shortcut.py
```

## Запуск из VS Code

- Нажмите `Ctrl+Shift+B`, чтобы настроить и собрать приложение.
- Для сборки и запуска выберите **Tasks: Run Task → CMake: Run**.

Исходный код окна находится в `src/main.cpp`. CMake создаёт файлы сборки в
папке `build/`; её содержимое не нужно добавлять в Git.
