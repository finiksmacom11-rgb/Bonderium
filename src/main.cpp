#include <windows.h>

#include "updater.h"

#include <cwchar>

namespace
{
constexpr char window_class_name[] = "BonderiumWindow";
constexpr char window_title[] = "bonderium";
constexpr COLORREF background_color = RGB(245, 247, 250);
constexpr COLORREF grid_color = RGB(204, 214, 224);
constexpr COLORREF text_color = RGB(28, 40, 51);
constexpr COLORREF x_axis_color = RGB(194, 76, 76);
constexpr COLORREF y_axis_color = RGB(54, 112, 176);
constexpr COLORREF atom_color = RGB(45, 137, 190);
constexpr COLORREF nucleus_color = RGB(231, 112, 73);
constexpr int grid_step = 32;
constexpr int coordinate_label_interval = 2;
constexpr int coordinate_limit = 1000000;

struct WorldState
{
    int atom_x = 0;
    int atom_y = 0;
};

WorldState world_state;

void draw_axis(HDC device_context, int x1, int y1, int x2, int y2, COLORREF color)
{
    HPEN axis_pen = CreatePen(PS_SOLID, 2, color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, axis_pen));
    MoveToEx(device_context, x1, y1, nullptr);
    LineTo(device_context, x2, y2);
    SelectObject(device_context, previous_pen);
    DeleteObject(axis_pen);
}

void draw_coordinate_label(HDC device_context, int coordinate, RECT label_area, UINT alignment, COLORREF color)
{
    wchar_t label[16]{};
    std::swprintf(label, sizeof(label) / sizeof(label[0]), L"%d", coordinate);
    SetTextColor(device_context, color);
    DrawTextW(device_context, label, -1, &label_area, alignment | DT_SINGLELINE | DT_VCENTER);
}

void draw_world_coordinate_system(HDC device_context, const RECT& client_area)
{
    HBRUSH background_brush = CreateSolidBrush(background_color);
    FillRect(device_context, &client_area, background_brush);
    DeleteObject(background_brush);

    const int origin_x = (client_area.left + client_area.right) / 2;
    const int origin_y = (client_area.top + client_area.bottom) / 2;

    HPEN grid_pen = CreatePen(PS_SOLID, 1, grid_color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, grid_pen));

    for (int index = (client_area.left - origin_x) / grid_step - 1;
         origin_x + index * grid_step <= client_area.right;
         ++index)
    {
        const int x = origin_x + index * grid_step;
        MoveToEx(device_context, x, client_area.top, nullptr);
        LineTo(device_context, x, client_area.bottom);
    }

    for (int index = (client_area.top - origin_y) / grid_step - 1;
         origin_y + index * grid_step <= client_area.bottom;
         ++index)
    {
        const int y = origin_y + index * grid_step;
        MoveToEx(device_context, client_area.left, y, nullptr);
        LineTo(device_context, client_area.right, y);
    }

    SelectObject(device_context, previous_pen);
    DeleteObject(grid_pen);

    draw_axis(device_context, client_area.left, origin_y, client_area.right, origin_y, x_axis_color);
    draw_axis(device_context, origin_x, client_area.bottom, origin_x, client_area.top, y_axis_color);

    SetBkMode(device_context, TRANSPARENT);
    const int first_x_index = (client_area.left - origin_x) / grid_step - 1;
    const int last_x_index = (client_area.right - origin_x) / grid_step + 1;
    HPEN x_tick_pen = CreatePen(PS_SOLID, 1, x_axis_color);
    HPEN previous_tick_pen = static_cast<HPEN>(SelectObject(device_context, x_tick_pen));
    for (int index = first_x_index; index <= last_x_index; ++index)
    {
        const int x = origin_x + index * grid_step;
        if (x < client_area.left || x > client_area.right)
        {
            continue;
        }

        MoveToEx(device_context, x, origin_y - 4, nullptr);
        LineTo(device_context, x, origin_y + 5);
        if (index != 0 && index % coordinate_label_interval == 0)
        {
            RECT label_area{x - 22, origin_y + 7, x + 22, origin_y + 25};
            draw_coordinate_label(device_context, index, label_area, DT_CENTER, x_axis_color);
        }
    }
    SelectObject(device_context, previous_tick_pen);
    DeleteObject(x_tick_pen);

    const int first_y_index = (origin_y - client_area.bottom) / grid_step - 1;
    const int last_y_index = (origin_y - client_area.top) / grid_step + 1;
    HPEN y_tick_pen = CreatePen(PS_SOLID, 1, y_axis_color);
    previous_tick_pen = static_cast<HPEN>(SelectObject(device_context, y_tick_pen));
    for (int index = first_y_index; index <= last_y_index; ++index)
    {
        const int y = origin_y - index * grid_step;
        if (y < client_area.top || y > client_area.bottom)
        {
            continue;
        }

        MoveToEx(device_context, origin_x - 4, y, nullptr);
        LineTo(device_context, origin_x + 5, y);
        if (index != 0 && index % coordinate_label_interval == 0)
        {
            RECT label_area{origin_x - 36, y - 9, origin_x - 9, y + 9};
            draw_coordinate_label(device_context, index, label_area, DT_RIGHT, y_axis_color);
        }
    }
    SelectObject(device_context, previous_tick_pen);
    DeleteObject(y_tick_pen);

    RECT origin_label_area{origin_x + 7, origin_y + 7, origin_x + 29, origin_y + 25};
    draw_coordinate_label(device_context, 0, origin_label_area, DT_LEFT, text_color);
    RECT x_label_area{client_area.right - 28, origin_y + 8, client_area.right - 8, origin_y + 28};
    SetTextColor(device_context, x_axis_color);
    DrawTextW(device_context, L"X", -1, &x_label_area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT y_label_area{origin_x + 8, client_area.top + 4, origin_x + 28, client_area.top + 24};
    SetTextColor(device_context, y_axis_color);
    DrawTextW(device_context, L"Y", -1, &y_label_area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void draw_atom(HDC device_context, const RECT& client_area)
{
    const int origin_x = (client_area.left + client_area.right) / 2;
    const int origin_y = (client_area.top + client_area.bottom) / 2;
    const int atom_x = origin_x + world_state.atom_x * grid_step;
    const int atom_y = origin_y - world_state.atom_y * grid_step;

    HPEN orbit_pen = CreatePen(PS_SOLID, 1, atom_color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, orbit_pen));
    HBRUSH hollow_brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HBRUSH previous_brush = static_cast<HBRUSH>(SelectObject(device_context, hollow_brush));
    Ellipse(device_context, atom_x - 13, atom_y - 6, atom_x + 13, atom_y + 6);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(orbit_pen);

    HBRUSH nucleus_brush = CreateSolidBrush(nucleus_color);
    HBRUSH electron_brush = CreateSolidBrush(atom_color);
    HPEN outline_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    previous_pen = static_cast<HPEN>(SelectObject(device_context, outline_pen));

    HBRUSH selected_brush = static_cast<HBRUSH>(SelectObject(device_context, nucleus_brush));
    Ellipse(device_context, atom_x - 5, atom_y - 5, atom_x + 5, atom_y + 5);
    SelectObject(device_context, electron_brush);
    Ellipse(device_context, atom_x + 8, atom_y - 3, atom_x + 14, atom_y + 3);

    SelectObject(device_context, selected_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(outline_pen);
    DeleteObject(nucleus_brush);
    DeleteObject(electron_brush);
}

void draw_status(HDC device_context)
{
    wchar_t status[128]{};
    std::swprintf(
        status,
        sizeof(status) / sizeof(status[0]),
        L"\u0410\u0442\u043E\u043C: (%d, %d)    \u0421\u0442\u0440\u0435\u043B\u043A\u0438/WASD \u2014 \u0434\u0432\u0438\u0436\u0435\u043D\u0438\u0435    Home \u2014 \u0432 \u043D\u0430\u0447\u0430\u043B\u043E",
        world_state.atom_x,
        world_state.atom_y);
    SetTextColor(device_context, text_color);
    SetBkMode(device_context, TRANSPARENT);
    TextOutW(device_context, 12, 12, status, static_cast<int>(wcslen(status)));
}

void move_atom(int delta_x, int delta_y)
{
    if ((delta_x > 0 && world_state.atom_x < coordinate_limit)
        || (delta_x < 0 && world_state.atom_x > -coordinate_limit))
    {
        world_state.atom_x += delta_x;
    }

    if ((delta_y > 0 && world_state.atom_y < coordinate_limit)
        || (delta_y < 0 && world_state.atom_y > -coordinate_limit))
    {
        world_state.atom_y += delta_y;
    }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }

    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window, &paint);
        RECT client_area{};
        GetClientRect(window, &client_area);

        draw_world_coordinate_system(device_context, client_area);
        draw_atom(device_context, client_area);
        draw_status(device_context);

        EndPaint(window, &paint);
        return 0;
    }

    if (message == WM_KEYDOWN)
    {
        switch (w_param)
        {
        case VK_LEFT:
        case 'A':
            move_atom(-1, 0);
            break;
        case VK_RIGHT:
        case 'D':
            move_atom(1, 0);
            break;
        case VK_UP:
        case 'W':
            move_atom(0, 1);
            break;
        case VK_DOWN:
        case 'S':
            move_atom(0, -1);
            break;
        case VK_HOME:
            world_state.atom_x = 0;
            world_state.atom_y = 0;
            break;
        default:
            return DefWindowProcA(window, message, w_param, l_param);
        }

        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }

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
    window_class.hbrBackground = nullptr;
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
    check_for_updates(window);

    MSG message{};
    int result = 0;
    while ((result = GetMessageA(&message, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }

    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
