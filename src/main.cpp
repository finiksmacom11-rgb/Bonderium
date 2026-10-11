#include <windows.h>

#include "updater.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace
{
constexpr wchar_t window_title[] = L"Bonderium — Infinite Coordinate Plane";

constexpr int toolbar_height = 62;
constexpr int footer_height = 38;
constexpr UINT_PTR camera_timer_id = 1;
constexpr UINT camera_timer_interval_ms = 16;
constexpr int molecule_tool_left = 258;
constexpr int molecule_tool_width = 148;
constexpr int molecule_tool_top = 14;
constexpr int molecule_tool_height = 34;
constexpr int baseline_cell_size = 42;
constexpr int major_grid_interval = 5;
constexpr double minimum_zoom = 0.18;
constexpr double maximum_zoom = 5.0;
constexpr double zoom_factor = 1.15;
constexpr double camera_pan_speed_pixels = 300.0;
constexpr double camera_ease_seconds = 0.12;
constexpr double camera_stop_threshold = 0.015;

constexpr COLORREF canvas_color = RGB(10, 14, 22);
constexpr COLORREF toolbar_color = RGB(15, 20, 30);
constexpr COLORREF footer_color = RGB(13, 18, 27);
constexpr COLORREF grid_color = RGB(25, 34, 47);
constexpr COLORREF major_grid_color = RGB(36, 49, 65);
constexpr COLORREF border_color = RGB(37, 49, 66);
constexpr COLORREF primary_text_color = RGB(228, 236, 247);
constexpr COLORREF muted_text_color = RGB(128, 144, 164);
constexpr COLORREF x_axis_color = RGB(69, 214, 201);
constexpr COLORREF y_axis_color = RGB(160, 132, 255);
constexpr COLORREF molecule_color = RGB(69, 214, 201);
constexpr COLORREF molecule_fill_color = RGB(22, 53, 64);

struct ViewState
{
    double center_x = 0.0;
    double center_y = 0.0;
    double zoom = 1.0;
    double velocity_x = 0.0;
    double velocity_y = 0.0;
    ULONGLONG last_motion_tick = 0;
};

struct Molecule
{
    std::uint64_t id;
    double x;
    double y;
    double radius;
};

ViewState view_state;
std::vector<Molecule> molecules;
std::uint64_t next_molecule_id = 1;
bool molecule_tool_active = false;
bool pointer_over_scene = false;
POINT pointer_position{};

int scene_width(const RECT& client_area)
{
    return (std::max)(1, static_cast<int>(client_area.right - client_area.left));
}

RECT scene_area(const RECT& client_area)
{
    return RECT{
        client_area.left,
        client_area.top + toolbar_height,
        client_area.right,
        (std::max)(client_area.top + toolbar_height,
            client_area.bottom - footer_height)};
}

int rounded_pixel(double position)
{
    return static_cast<int>(std::lround(position));
}

void fill_rectangle(HDC device_context, const RECT& area, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(device_context, &area, brush);
    DeleteObject(brush);
}

void draw_line(HDC device_context, int x1, int y1, int x2, int y2,
    COLORREF color, int width = 1)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, pen));
    MoveToEx(device_context, x1, y1, nullptr);
    LineTo(device_context, x2, y2);
    SelectObject(device_context, previous_pen);
    DeleteObject(pen);
}

void draw_text(HDC device_context, const wchar_t* text, RECT area,
    COLORREF color, HFONT font, UINT alignment = DT_LEFT)
{
    HFONT previous_font = static_cast<HFONT>(SelectObject(device_context, font));
    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, color);
    DrawTextW(device_context, text, -1, &area,
        alignment | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(device_context, previous_font);
}

void format_coordinate(wchar_t* buffer, std::size_t buffer_size, double value)
{
    std::swprintf(buffer, buffer_size, L"%.0f", value);
}

int world_to_screen_x(double world_x, int center_x, double cell_size)
{
    return rounded_pixel(center_x + (world_x - view_state.center_x) * cell_size);
}

int world_to_screen_y(double world_y, int center_y, double cell_size)
{
    return rounded_pixel(center_y - (world_y - view_state.center_y) * cell_size);
}

RECT molecule_tool_area(const RECT& client_area)
{
    return RECT{
        client_area.left + molecule_tool_left,
        client_area.top + molecule_tool_top,
        client_area.left + molecule_tool_left + molecule_tool_width,
        client_area.top + molecule_tool_top + molecule_tool_height};
}

void draw_molecule(HDC device_context, int center_x, int center_y, int radius,
    std::uint64_t id, HFONT label_font)
{
    HPEN halo_pen = CreatePen(PS_SOLID, 1, RGB(34, 91, 101));
    HBRUSH hollow_brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, halo_pen));
    HBRUSH previous_brush = static_cast<HBRUSH>(SelectObject(device_context, hollow_brush));
    Ellipse(device_context, center_x - radius - 5, center_y - radius - 5,
        center_x + radius + 6, center_y + radius + 6);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(halo_pen);

    HPEN outline_pen = CreatePen(PS_SOLID, 2, molecule_color);
    HBRUSH fill_brush = CreateSolidBrush(molecule_fill_color);
    previous_pen = static_cast<HPEN>(SelectObject(device_context, outline_pen));
    previous_brush = static_cast<HBRUSH>(SelectObject(device_context, fill_brush));
    Ellipse(device_context, center_x - radius, center_y - radius,
        center_x + radius + 1, center_y + radius + 1);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(fill_brush);
    DeleteObject(outline_pen);

    const int highlight_radius = (std::max)(2, radius / 5);
    HBRUSH highlight_brush = CreateSolidBrush(RGB(143, 246, 229));
    previous_brush = static_cast<HBRUSH>(SelectObject(device_context, highlight_brush));
    Ellipse(device_context, center_x - highlight_radius, center_y - highlight_radius,
        center_x + highlight_radius + 1, center_y + highlight_radius + 1);
    SelectObject(device_context, previous_brush);
    DeleteObject(highlight_brush);

    if (radius >= 13)
    {
        wchar_t label[24]{};
        std::swprintf(label, sizeof(label) / sizeof(label[0]), L"M%llu",
            static_cast<unsigned long long>(id));
        RECT label_area{center_x - radius, center_y + radius + 6,
            center_x + radius + 1, center_y + radius + 23};
        draw_text(device_context, label, label_area, primary_text_color,
            label_font, DT_CENTER);
    }
}

void draw_molecules(HDC device_context, const RECT& area, int center_x,
    int center_y, double cell_size, HFONT label_font)
{
    const int saved_state = SaveDC(device_context);
    IntersectClipRect(device_context, area.left, area.top, area.right, area.bottom);
    for (const Molecule& molecule : molecules)
    {
        const double offset_x = (molecule.x - view_state.center_x) * cell_size;
        const double offset_y = -(molecule.y - view_state.center_y) * cell_size;
        const double radius = molecule.radius * cell_size;
        if (std::abs(offset_x) > (area.right - area.left) / 2.0 + radius + 8.0 ||
            std::abs(offset_y) > (area.bottom - area.top) / 2.0 + radius + 28.0)
        {
            continue;
        }

        const int screen_x = rounded_pixel(center_x + offset_x);
        const int screen_y = rounded_pixel(center_y + offset_y);
        draw_molecule(device_context, screen_x, screen_y,
            (std::max)(2, rounded_pixel(radius)), molecule.id, label_font);
    }

    if (molecule_tool_active && pointer_over_scene)
    {
        if (pointer_position.x >= area.left && pointer_position.x < area.right &&
            pointer_position.y >= area.top && pointer_position.y < area.bottom)
        {
            const double radius = 0.42 * cell_size;
            const int screen_radius = (std::max)(2, rounded_pixel(radius));
            HPEN preview_pen = CreatePen(PS_DASH, 1, molecule_color);
            HBRUSH hollow_brush =
                static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
            HPEN previous_pen =
                static_cast<HPEN>(SelectObject(device_context, preview_pen));
            HBRUSH previous_brush =
                static_cast<HBRUSH>(SelectObject(device_context, hollow_brush));
            Ellipse(device_context,
                pointer_position.x - screen_radius,
                pointer_position.y - screen_radius,
                pointer_position.x + screen_radius + 1,
                pointer_position.y + screen_radius + 1);
            SelectObject(device_context, previous_brush);
            SelectObject(device_context, previous_pen);
            DeleteObject(preview_pen);

            draw_line(device_context, pointer_position.x - 4,
                pointer_position.y, pointer_position.x + 5,
                pointer_position.y, molecule_color);
            draw_line(device_context, pointer_position.x,
                pointer_position.y - 4, pointer_position.x,
                pointer_position.y + 5, molecule_color);
        }
    }
    RestoreDC(device_context, saved_state);
}

void draw_grid(HDC device_context, const RECT& area, int center_x, int center_y,
    double cell_size)
{
    const double first_x = std::floor(
        view_state.center_x - static_cast<double>(center_x - area.left) / cell_size) - 1.0;
    const int vertical_lines = static_cast<int>(
        std::ceil(static_cast<double>(area.right - area.left) / cell_size)) + 3;

    for (int index = 0; index < vertical_lines; ++index)
    {
        const double coordinate = first_x + index;
        const int x = world_to_screen_x(coordinate, center_x, cell_size);
        if (x < area.left || x > area.right)
        {
            continue;
        }

        const bool major = std::fmod(std::abs(coordinate), major_grid_interval) < 0.001;
        draw_line(device_context, x, area.top, x, area.bottom,
            major ? major_grid_color : grid_color);
    }

    const double first_y = std::floor(
        view_state.center_y - static_cast<double>(center_y - area.top) / cell_size) - 1.0;
    const int horizontal_lines = static_cast<int>(
        std::ceil(static_cast<double>(area.bottom - area.top) / cell_size)) + 3;

    for (int index = 0; index < horizontal_lines; ++index)
    {
        const double coordinate = first_y + index;
        const int y = world_to_screen_y(coordinate, center_y, cell_size);
        if (y < area.top || y > area.bottom)
        {
            continue;
        }

        const bool major = std::fmod(std::abs(coordinate), major_grid_interval) < 0.001;
        draw_line(device_context, area.left, y, area.right, y,
            major ? major_grid_color : grid_color);
    }
}

void draw_axes(HDC device_context, const RECT& area, int center_x, int center_y,
    double cell_size, HFONT label_font)
{
    const int x_axis_y = world_to_screen_y(0.0, center_y, cell_size);
    const int y_axis_x = world_to_screen_x(0.0, center_x, cell_size);
    const double label_interval =
        std::ceil(72.0 / (cell_size * major_grid_interval)) * major_grid_interval;

    if (x_axis_y >= area.top && x_axis_y <= area.bottom)
    {
        draw_line(device_context, area.left, x_axis_y, area.right, x_axis_y,
            x_axis_color, 2);
        const double first_x = std::ceil(view_state.center_x
            - static_cast<double>(center_x - area.left) / cell_size);
        const int tick_count = static_cast<int>(
            std::ceil(static_cast<double>(area.right - area.left) / cell_size)) + 2;
        for (int index = 0; index < tick_count; ++index)
        {
            const double coordinate = first_x + index;
            const int x = world_to_screen_x(coordinate, center_x, cell_size);
            if (x < area.left || x > area.right)
            {
                continue;
            }
            draw_line(device_context, x, x_axis_y - 3, x, x_axis_y + 4,
                x_axis_color);

            if (std::fmod(std::abs(coordinate), label_interval) < 0.001)
            {
                wchar_t label[32]{};
                format_coordinate(label, sizeof(label) / sizeof(label[0]), coordinate);
                RECT label_area{x - 25, x_axis_y + 7, x + 25, x_axis_y + 27};
                if (label_area.bottom > area.bottom - 2)
                {
                    label_area.top = x_axis_y - 27;
                    label_area.bottom = x_axis_y - 7;
                }
                draw_text(device_context, label, label_area, muted_text_color, label_font,
                    DT_CENTER);
            }
        }
    }

    if (y_axis_x >= area.left && y_axis_x <= area.right)
    {
        draw_line(device_context, y_axis_x, area.top, y_axis_x, area.bottom,
            y_axis_color, 2);
        const double first_y = std::ceil(view_state.center_y
            + static_cast<double>(center_y - area.top) / cell_size);
        const int tick_count = static_cast<int>(
            std::ceil(static_cast<double>(area.bottom - area.top) / cell_size)) + 2;
        for (int index = 0; index < tick_count; ++index)
        {
            const double coordinate = first_y - index;
            const int y = world_to_screen_y(coordinate, center_y, cell_size);
            if (y < area.top || y > area.bottom)
            {
                continue;
            }
            draw_line(device_context, y_axis_x - 3, y, y_axis_x + 4, y,
                y_axis_color);

            if (std::fmod(std::abs(coordinate), label_interval) < 0.001)
            {
                wchar_t label[32]{};
                format_coordinate(label, sizeof(label) / sizeof(label[0]), coordinate);
                const int label_left = y_axis_x - 55 >= area.left
                    ? y_axis_x - 51
                    : y_axis_x + 9;
                RECT label_area{label_left, y - 10, label_left + 42, y + 10};
                draw_text(device_context, label, label_area, muted_text_color, label_font,
                    DT_RIGHT);
            }
        }
    }

    if (x_axis_y >= area.top && x_axis_y <= area.bottom &&
        y_axis_x >= area.left && y_axis_x <= area.right)
    {
        RECT origin_area{y_axis_x + 9, x_axis_y + 7, y_axis_x + 35, x_axis_y + 27};
        if (origin_area.bottom > area.bottom - 2)
        {
            origin_area.top = x_axis_y - 27;
            origin_area.bottom = x_axis_y - 7;
        }
        draw_text(device_context, L"0", origin_area, primary_text_color, label_font);
    }

    const int safe_x_axis_y = (std::clamp)(x_axis_y,
        static_cast<int>(area.top) + 12, static_cast<int>(area.bottom) - 12);
    const int safe_y_axis_x = (std::clamp)(y_axis_x,
        static_cast<int>(area.left) + 12, static_cast<int>(area.right) - 12);
    RECT x_label{area.right - 28, safe_x_axis_y - 12,
        area.right - 8, safe_x_axis_y + 12};
    RECT y_label{safe_y_axis_x - 12, area.top + 8,
        safe_y_axis_x + 12, area.top + 32};
    draw_text(device_context, L"X", x_label, x_axis_color, label_font, DT_CENTER);
    draw_text(device_context, L"Y", y_label, y_axis_color, label_font, DT_CENTER);
}

void draw_toolbar(HDC device_context, const RECT& client_area, HFONT title_font,
    HFONT small_font, HFONT zoom_font)
{
    RECT toolbar{client_area.left, client_area.top, client_area.right,
        client_area.top + toolbar_height};
    fill_rectangle(device_context, toolbar, toolbar_color);
    draw_line(device_context, client_area.left, toolbar.bottom - 1,
        client_area.right, toolbar.bottom - 1, border_color);

    const int brand_center_y = toolbar.top + toolbar_height / 2;
    HBRUSH accent_brush = CreateSolidBrush(x_axis_color);
    HBRUSH previous_brush = static_cast<HBRUSH>(SelectObject(device_context, accent_brush));
    HPEN accent_pen = CreatePen(PS_SOLID, 1, x_axis_color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, accent_pen));
    Ellipse(device_context, 20, brand_center_y - 9, 38, brand_center_y + 9);
    SelectObject(device_context, previous_pen);
    SelectObject(device_context, previous_brush);
    DeleteObject(accent_pen);
    DeleteObject(accent_brush);

    RECT title_area{49, toolbar.top + 9, 225, toolbar.top + 35};
    draw_text(device_context, L"BONDERIUM", title_area, primary_text_color, title_font);
    RECT subtitle_area{50, toolbar.top + 34, 225, toolbar.top + 51};
    draw_text(device_context, L"INFINITE COORDINATE PLANE", subtitle_area,
        muted_text_color, small_font);

    RECT molecule_button = molecule_tool_area(client_area);
    const COLORREF button_fill = molecule_tool_active
        ? RGB(26, 55, 61)
        : RGB(20, 27, 39);
    const COLORREF button_outline = molecule_tool_active
        ? RGB(69, 142, 139)
        : border_color;
    HPEN button_pen = CreatePen(PS_SOLID, 1, button_outline);
    HBRUSH button_brush = CreateSolidBrush(button_fill);
    HPEN previous_button_pen =
        static_cast<HPEN>(SelectObject(device_context, button_pen));
    HBRUSH previous_button_brush =
        static_cast<HBRUSH>(SelectObject(device_context, button_brush));
    RoundRect(device_context, molecule_button.left, molecule_button.top,
        molecule_button.right, molecule_button.bottom, 10, 10);
    SelectObject(device_context, previous_button_brush);
    SelectObject(device_context, previous_button_pen);
    DeleteObject(button_brush);
    DeleteObject(button_pen);
    RECT molecule_label{molecule_button.left + 10, molecule_button.top + 1,
        molecule_button.right - 9, molecule_button.bottom - 1};
    draw_text(device_context, L"+  Molecule", molecule_label,
        molecule_tool_active ? x_axis_color : primary_text_color,
        zoom_font, DT_CENTER);

    const int zoom_percent = rounded_pixel(view_state.zoom * 100.0);
    wchar_t zoom_label[32]{};
    std::swprintf(zoom_label, sizeof(zoom_label) / sizeof(zoom_label[0]),
        L"%d%%", zoom_percent);
    RECT zoom_area{client_area.right - 92, toolbar.top + 16,
        client_area.right - 22, toolbar.top + 46};
    HPEN outline_pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH pill_brush = CreateSolidBrush(RGB(20, 27, 39));
    HPEN previous_outline = static_cast<HPEN>(SelectObject(device_context, outline_pen));
    HBRUSH previous_pill = static_cast<HBRUSH>(SelectObject(device_context, pill_brush));
    RoundRect(device_context, zoom_area.left, zoom_area.top, zoom_area.right,
        zoom_area.bottom, 10, 10);
    SelectObject(device_context, previous_pill);
    SelectObject(device_context, previous_outline);
    DeleteObject(pill_brush);
    DeleteObject(outline_pen);
    RECT zoom_text_area{zoom_area.left + 4, zoom_area.top, zoom_area.right - 4,
        zoom_area.bottom};
    draw_text(device_context, zoom_label, zoom_text_area, primary_text_color,
        zoom_font, DT_CENTER);
}

void draw_footer(HDC device_context, const RECT& client_area, HFONT small_font,
    HFONT status_font)
{
    RECT footer{client_area.left, client_area.bottom - footer_height,
        client_area.right, client_area.bottom};
    fill_rectangle(device_context, footer, footer_color);
    draw_line(device_context, client_area.left, footer.top,
        client_area.right, footer.top, border_color);

    RECT instructions{20, footer.top + 3, client_area.right - 380, footer.bottom - 3};
    const wchar_t* hint = molecule_tool_active
        ? L"CLICK TO PLACE MOLECULE     ESC CANCEL"
        : L"WASD / ARROWS  PAN   SCROLL  ZOOM   C  MOLECULE";
    draw_text(device_context, hint, instructions, muted_text_color, small_font);

    wchar_t coordinates[128]{};
    std::swprintf(coordinates, sizeof(coordinates) / sizeof(coordinates[0]),
        L"MOLECULES %llu   X %.2f   Y %.2f",
        static_cast<unsigned long long>(molecules.size()),
        view_state.center_x, view_state.center_y);
    RECT status_area{client_area.right - 368, footer.top + 3,
        client_area.right - 18, footer.bottom - 3};
    draw_text(device_context, coordinates, status_area, primary_text_color,
        status_font, DT_RIGHT);
}

void draw_scene(HDC device_context, const RECT& client_area)
{
    fill_rectangle(device_context, client_area, canvas_color);

    const RECT area = scene_area(client_area);
    const double cell_size = baseline_cell_size * view_state.zoom;
    const int center_x = area.left + (area.right - area.left) / 2;
    const int center_y = area.top + (area.bottom - area.top) / 2;

    HFONT label_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT title_font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT small_font = CreateFontW(-10, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT zoom_font = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT status_font = CreateFontW(-11, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (label_font != nullptr && title_font != nullptr && small_font != nullptr &&
        zoom_font != nullptr && status_font != nullptr)
    {
        draw_grid(device_context, area, center_x, center_y, cell_size);
        draw_axes(device_context, area, center_x, center_y, cell_size, label_font);
        draw_molecules(device_context, area, center_x, center_y, cell_size, label_font);
        draw_toolbar(device_context, client_area, title_font, small_font, zoom_font);
        draw_footer(device_context, client_area, small_font, status_font);
    }

    if (label_font != nullptr)
    {
        DeleteObject(label_font);
    }
    if (title_font != nullptr)
    {
        DeleteObject(title_font);
    }
    if (small_font != nullptr)
    {
        DeleteObject(small_font);
    }
    if (zoom_font != nullptr)
    {
        DeleteObject(zoom_font);
    }
    if (status_font != nullptr)
    {
        DeleteObject(status_font);
    }
}

bool is_pan_key_down(int primary_key, int alternate_key)
{
    return (GetAsyncKeyState(primary_key) & 0x8000) != 0 ||
        (GetAsyncKeyState(alternate_key) & 0x8000) != 0;
}

void start_camera_motion(HWND window)
{
    view_state.last_motion_tick = GetTickCount64();
    SetTimer(window, camera_timer_id, camera_timer_interval_ms, nullptr);
}

void set_molecule_tool(HWND window, bool active)
{
    molecule_tool_active = active;
    pointer_over_scene = false;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT area = scene_area(client_area);
    const bool over_scene = active && PtInRect(&area, cursor);
    SetCursor(LoadCursorW(nullptr,
        MAKEINTRESOURCEW(over_scene ? 32515 : 32512)));
    InvalidateRect(window, nullptr, FALSE);
}

void stop_camera_motion(HWND window)
{
    view_state.velocity_x = 0.0;
    view_state.velocity_y = 0.0;
    view_state.last_motion_tick = 0;
    KillTimer(window, camera_timer_id);
}

void update_camera_motion(HWND window)
{
    const ULONGLONG now = GetTickCount64();
    if (view_state.last_motion_tick == 0)
    {
        view_state.last_motion_tick = now;
        return;
    }

    const double elapsed = (std::min)(0.05,
        static_cast<double>(now - view_state.last_motion_tick) / 1000.0);
    view_state.last_motion_tick = now;

    const int horizontal = static_cast<int>(is_pan_key_down('D', VK_RIGHT)) -
        static_cast<int>(is_pan_key_down('A', VK_LEFT));
    const int vertical = static_cast<int>(is_pan_key_down('W', VK_UP)) -
        static_cast<int>(is_pan_key_down('S', VK_DOWN));
    const double diagonal_scale = horizontal != 0 && vertical != 0
        ? 0.7071067811865476
        : 1.0;
    const double world_speed = camera_pan_speed_pixels /
        (baseline_cell_size * view_state.zoom);
    const double target_x = horizontal * diagonal_scale * world_speed;
    const double target_y = vertical * diagonal_scale * world_speed;
    const double easing = 1.0 - std::exp(-elapsed / camera_ease_seconds);

    view_state.velocity_x += (target_x - view_state.velocity_x) * easing;
    view_state.velocity_y += (target_y - view_state.velocity_y) * easing;
    view_state.center_x += view_state.velocity_x * elapsed;
    view_state.center_y += view_state.velocity_y * elapsed;

    InvalidateRect(window, nullptr, FALSE);
    if (horizontal == 0 && vertical == 0 &&
        std::abs(view_state.velocity_x) < camera_stop_threshold &&
        std::abs(view_state.velocity_y) < camera_stop_threshold)
    {
        stop_camera_motion(window);
    }
}

void reset_view()
{
    view_state.center_x = 0.0;
    view_state.center_y = 0.0;
    view_state.zoom = 1.0;
    view_state.velocity_x = 0.0;
    view_state.velocity_y = 0.0;
    view_state.last_motion_tick = 0;
}

void place_molecule(const RECT& client_area, POINT position)
{
    const RECT area = scene_area(client_area);
    const double cell_size = baseline_cell_size * view_state.zoom;
    const int center_x = area.left + (area.right - area.left) / 2;
    const int center_y = area.top + (area.bottom - area.top) / 2;
    molecules.push_back(Molecule{
        next_molecule_id++,
        view_state.center_x + static_cast<double>(position.x - center_x) / cell_size,
        view_state.center_y - static_cast<double>(position.y - center_y) / cell_size,
        0.42});
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
        HDC window_context = BeginPaint(window, &paint);
        RECT client_area{};
        GetClientRect(window, &client_area);

        HDC buffer_context = CreateCompatibleDC(window_context);
        HBITMAP buffer_bitmap = CreateCompatibleBitmap(window_context,
            scene_width(client_area), (std::max)(1, static_cast<int>(client_area.bottom)));
        if (buffer_context != nullptr && buffer_bitmap != nullptr)
        {
            HBITMAP previous_bitmap =
                static_cast<HBITMAP>(SelectObject(buffer_context, buffer_bitmap));
            draw_scene(buffer_context, client_area);
            BitBlt(window_context, 0, 0, scene_width(client_area),
                (std::max)(1, static_cast<int>(client_area.bottom)),
                buffer_context, 0, 0, SRCCOPY);
            SelectObject(buffer_context, previous_bitmap);
        }
        else
        {
            draw_scene(window_context, client_area);
        }

        if (buffer_bitmap != nullptr)
        {
            DeleteObject(buffer_bitmap);
        }
        if (buffer_context != nullptr)
        {
            DeleteDC(buffer_context);
        }
        EndPaint(window, &paint);
        return 0;
    }

    if (message == WM_KEYDOWN)
    {
        if (w_param == VK_ESCAPE && molecule_tool_active)
        {
            set_molecule_tool(window, false);
            return 0;
        }

        if (w_param == 'C' && (l_param & (1L << 30)) == 0)
        {
            set_molecule_tool(window, !molecule_tool_active);
            return 0;
        }

        switch (w_param)
        {
        case VK_LEFT:
        case 'A':
        case VK_RIGHT:
        case 'D':
        case VK_UP:
        case 'W':
        case VK_DOWN:
        case 'S':
            start_camera_motion(window);
            break;
        case VK_HOME:
            reset_view();
            KillTimer(window, camera_timer_id);
            break;
        default:
            return DefWindowProcW(window, message, w_param, l_param);
        }

        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }

    if (message == WM_MOUSEMOVE)
    {
        POINT position{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        const bool over_scene = PtInRect(&area, position) != FALSE;
        const bool changed = position.x != pointer_position.x ||
            position.y != pointer_position.y || over_scene != pointer_over_scene;
        pointer_position = position;
        pointer_over_scene = over_scene;

        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);

        if (molecule_tool_active)
        {
            SetCursor(LoadCursorW(nullptr,
                MAKEINTRESOURCEW(over_scene ? 32515 : 32512)));
        }
        if (changed && molecule_tool_active)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    if (message == WM_MOUSELEAVE)
    {
        pointer_over_scene = false;
        if (molecule_tool_active)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    if (message == WM_LBUTTONDOWN)
    {
        const POINT position{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT button_area = molecule_tool_area(client_area);
        if (PtInRect(&button_area, position))
        {
            SetFocus(window);
            set_molecule_tool(window, !molecule_tool_active);
            return 0;
        }

        const RECT area = scene_area(client_area);
        if (molecule_tool_active && PtInRect(&area, position))
        {
            place_molecule(client_area, position);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
    }

    if (message == WM_SETCURSOR && molecule_tool_active &&
        LOWORD(l_param) == HTCLIENT)
    {
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(window, &cursor);
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        SetCursor(LoadCursorW(nullptr,
            MAKEINTRESOURCEW(PtInRect(&area, cursor) ? 32515 : 32512)));
        return TRUE;
    }

    if (message == WM_TIMER && w_param == camera_timer_id)
    {
        update_camera_motion(window);
        return 0;
    }

    if (message == WM_KILLFOCUS)
    {
        stop_camera_motion(window);
    }

    if (message == WM_MOUSEWHEEL)
    {
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        POINT cursor{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        ScreenToClient(window, &cursor);
        if (cursor.y >= area.top && cursor.y < area.bottom)
        {
            const double old_cell_size = baseline_cell_size * view_state.zoom;
            const int center_x = area.left + (area.right - area.left) / 2;
            const int center_y = area.top + (area.bottom - area.top) / 2;
            const double world_x = view_state.center_x +
                static_cast<double>(cursor.x - center_x) / old_cell_size;
            const double world_y = view_state.center_y -
                static_cast<double>(cursor.y - center_y) / old_cell_size;

            const double wheel_steps =
                static_cast<double>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA;
            view_state.zoom = (std::clamp)(view_state.zoom *
                std::pow(zoom_factor, wheel_steps), minimum_zoom, maximum_zoom);

            const double new_cell_size = baseline_cell_size * view_state.zoom;
            view_state.center_x = world_x -
                static_cast<double>(cursor.x - center_x) / new_cell_size;
            view_state.center_y = world_y +
                static_cast<double>(cursor.y - center_y) / new_cell_size;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        limits->ptMinTrackSize.x = 640;
        limits->ptMinTrackSize.y = 420;
        return 0;
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command)
{
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = L"BonderiumWindow";

    if (RegisterClassW(&window_class) == 0)
    {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        window_class.lpszClassName,
        window_title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        760,
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
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
