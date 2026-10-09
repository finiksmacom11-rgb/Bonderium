#include <windows.h>

namespace
{
constexpr char window_class_name[] = "BonderiumWindow";
constexpr char window_title[] = "bonderium";

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(window, message, w_param, l_param);
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command)
{
    WNDCLASSA window_class{};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = window_class_name;

    if (RegisterClassA(&window_class) == 0)
    {
        return 1;
    }

    HWND window = CreateWindowExA(
        0,
        window_class_name,
        window_title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr)
    {
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    int result = 0;
    while ((result = GetMessageA(&message, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
