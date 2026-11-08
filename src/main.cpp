#include <windows.h>
#include <GL/gl.h>
#include <psapi.h>
#include <shellapi.h>

#include "elements.h"
#include "simulation.h"
#include "updater.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr wchar_t window_title[] = L"Bonderium — 3D Molecular Space";
constexpr wchar_t window_class_name[] = L"BonderiumWindow";
constexpr wchar_t detached_interface_class_name[] =
    L"BonderiumDetachedInterface";
constexpr wchar_t detached_settings_class_name[] =
    L"BonderiumDetachedSettings";
constexpr wchar_t chart_window_class_name[] = L"BonderiumChartWindow";
constexpr wchar_t element_table_window_class_name[] =
    L"BonderiumElementTable";

constexpr int toolbar_height = 62;
constexpr int footer_height = 116;
constexpr int tools_panel_width = 220;
constexpr int workspace_rail_width = 60;
constexpr int interaction_tool_left = 798;
constexpr int interaction_tool_width = 112;
constexpr int interaction_tool_top = 14;
constexpr int interaction_tool_height = 34;
constexpr int auto_config_tool_left = 920;
constexpr int auto_config_tool_width = 66;
constexpr int auto_config_tool_top = 14;
constexpr int auto_config_tool_height = 34;
constexpr int restart_tool_left = 994;
constexpr int restart_tool_width = 78;
constexpr int restart_tool_top = 14;
constexpr int restart_tool_height = 34;
constexpr UINT_PTR camera_timer_id = 1;
constexpr UINT_PTR telemetry_timer_id = 2;
constexpr UINT_PTR simulation_timer_id = 3;
constexpr UINT_PTR motion_timer_interval_ms = 16;
constexpr UINT simulation_timer_interval_ms = 8;
constexpr unsigned int maximum_catch_up_steps = 12;
constexpr double minimum_time_flow_factor = 0.25;
constexpr double maximum_time_flow_factor = 4.0;
constexpr std::size_t chart_sample_capacity = 3000;
constexpr std::size_t maximum_chart_particles = 8;
constexpr std::uint64_t chart_sample_step_stride = 10;

constexpr double camera_default_distance = 28.0;
constexpr double camera_minimum_distance = 3.0;
constexpr double camera_maximum_distance = 240.0;
constexpr double camera_zoom_factor = 1.15;
constexpr double camera_move_speed = 12.0;
constexpr double camera_ease_seconds = 0.12;
constexpr double camera_stop_threshold = 0.015;
constexpr double molecule_radius = 0.42;
constexpr double spring_drag_stiffness = 30.0;
constexpr double directional_force_scale = 18.0;
constexpr double maximum_interaction_force = 200.0;
constexpr double field_of_view = 45.0;
constexpr double pi = 3.14159265358979323846;

COLORREF canvas_color = RGB(10, 14, 22);
COLORREF toolbar_color = RGB(15, 20, 30);
COLORREF footer_color = RGB(13, 18, 27);
COLORREF border_color = RGB(37, 49, 66);
COLORREF primary_text_color = RGB(228, 236, 247);
COLORREF muted_text_color = RGB(128, 144, 164);
COLORREF x_axis_color = RGB(69, 214, 201);
COLORREF y_axis_color = RGB(160, 132, 255);
COLORREF z_axis_color = RGB(255, 174, 94);
COLORREF surface_color = RGB(20, 27, 39);
COLORREF active_surface_color = RGB(26, 55, 61);

enum class VisualStyle
{
    deep_space,
    arctic,
    forest,
    amber,
    orchid
};

enum class WorkspacePage
{
    closed,
    objects,
    thermodynamics,
    dynamics,
    quantum,
    settings,
    experiments
};

struct VisualPalette
{
    COLORREF canvas;
    COLORREF toolbar;
    COLORREF panel;
    COLORREF border;
    COLORREF text;
    COLORREF muted;
    COLORREF accent;
    COLORREF secondary;
    COLORREF tertiary;
    COLORREF surface;
    COLORREF active_surface;
};

constexpr VisualPalette visual_palettes[]{
    {RGB(10, 14, 22), RGB(15, 20, 30), RGB(13, 18, 27),
        RGB(37, 49, 66), RGB(228, 236, 247), RGB(128, 144, 164),
        RGB(69, 214, 201), RGB(160, 132, 255), RGB(255, 174, 94),
        RGB(20, 27, 39), RGB(26, 55, 61)},
    {RGB(12, 21, 31), RGB(19, 32, 45), RGB(16, 28, 40),
        RGB(45, 70, 88), RGB(229, 242, 250), RGB(135, 165, 184),
        RGB(92, 207, 240), RGB(120, 171, 235), RGB(113, 220, 199),
        RGB(24, 42, 57), RGB(31, 72, 91)},
    {RGB(12, 22, 20), RGB(19, 34, 30), RGB(16, 29, 26),
        RGB(43, 72, 59), RGB(230, 242, 232), RGB(142, 171, 151),
        RGB(104, 214, 156), RGB(152, 205, 132), RGB(228, 190, 104),
        RGB(23, 43, 35), RGB(31, 72, 53)},
    {RGB(25, 17, 13), RGB(38, 27, 20), RGB(31, 23, 18),
        RGB(83, 60, 42), RGB(250, 239, 222), RGB(184, 155, 126),
        RGB(255, 177, 91), RGB(237, 129, 83), RGB(244, 211, 126),
        RGB(49, 34, 24), RGB(87, 52, 29)},
    {RGB(20, 15, 29), RGB(31, 23, 44), RGB(26, 20, 38),
        RGB(67, 52, 91), RGB(240, 232, 250), RGB(166, 148, 191),
        RGB(202, 132, 255), RGB(132, 162, 255), RGB(255, 142, 203),
        RGB(38, 28, 55), RGB(69, 42, 91)}
};

enum class GraphicsQuality
{
    performance,
    balanced,
    quality
};

enum GraphicsMenuCommand : UINT
{
    menu_toggle_fullscreen = 1,
    menu_quality_performance,
    menu_quality_balanced,
    menu_quality_high,
    menu_toggle_grid,
    menu_ram_unlimited = 20,
    menu_ram_256_mb,
    menu_ram_512_mb,
    menu_ram_1_gb,
    menu_ram_2_gb,
    menu_ram_4_gb,
    menu_ram_8_gb,
    menu_ram_custom,
    menu_panel_toolbar = 40,
    menu_panel_footer,
    menu_panel_tools,
    menu_separate_interface = 60,
    menu_open_detached_settings,
    menu_open_charts
};

enum class InteractionTool
{
    select,
    spring_drag,
    directional_force
};

enum class MolecularTool
{
    select,
    multi_select,
    square_lattice
};

enum class ScenarioAction
{
    apply_force,
    spawn_lattice
};

struct ScenarioStep
{
    ScenarioAction action = ScenarioAction::apply_force;
    double duration_seconds = 1.0;
    simulation::Vec3 force;
    std::vector<std::size_t> targets;
    std::size_t lattice_count = 0;
    simulation::Vec3 lattice_start;
    simulation::Vec3 lattice_end;
};

struct InputDialogState
{
    HWND edits[4]{};
    const wchar_t* title = nullptr;
    const wchar_t* labels[4]{};
    wchar_t values[4][32]{};
    std::size_t field_count = 0;
    bool done = false;
    bool accepted = false;
};

enum InteractionMenuCommand : UINT
{
    menu_interaction_select = 101,
    menu_interaction_spring_drag,
    menu_interaction_directional_force
};

enum ScenarioMenuCommand : UINT
{
    menu_scenario_force_step = 201,
    menu_scenario_lattice_step
};

using simulation::Vec3;

struct CameraState
{
    Vec3 target{};
    Vec3 velocity{};
    double yaw = -pi / 4.0;
    double pitch = pi / 3.0;
    double distance = camera_default_distance;
    ULONGLONG last_motion_tick = 0;
};

struct Telemetry
{
    ULONGLONG started_at = 0;
    ULONGLONG frame_sample_started_at = 0;
    unsigned int frames_since_sample = 0;
    double frames_per_second = 0.0;
    double frame_time_ms = 0.0;
    double simulation_steps_per_second = 0.0;
    bool has_frame_sample = false;
    std::uint64_t last_simulation_step_sample = 0;
    ULONGLONG simulation_sample_started_at = 0;
};

struct ChartSample
{
    double time = 0.0;
    std::array<double, 5> system_values{};
    std::array<std::array<double, 8>, maximum_chart_particles>
        particle_values{};
};

struct CameraBasis
{
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

CameraState camera_state;
Telemetry telemetry;
LARGE_INTEGER performance_frequency{};
simulation::Engine simulation_engine;
HDC window_device_context = nullptr;
HGLRC open_gl_context = nullptr;
HFONT title_font = nullptr;
HFONT small_font = nullptr;
HFONT button_font = nullptr;
HFONT status_font = nullptr;
GraphicsQuality graphics_quality = GraphicsQuality::balanced;
std::uint64_t ram_budget_bytes = 0;
std::uint64_t process_private_bytes = 0;
bool process_memory_available = false;
bool grid_visible = true;
bool toolbar_visible = true;
bool footer_visible = false;
bool tools_panel_visible = false;
VisualStyle visual_style = VisualStyle::deep_space;
WorkspacePage workspace_page = WorkspacePage::closed;
bool fullscreen = false;
bool separate_interface_windows = false;
double time_flow_factor = 1.0;
bool time_slider_dragging = false;
WINDOWPLACEMENT windowed_placement{};
LONG_PTR windowed_style = 0;
LONG_PTR windowed_extended_style = 0;
HWND main_window = nullptr;
HWND detached_interface_window = nullptr;
HWND detached_settings_window = nullptr;
HBRUSH detached_settings_background = nullptr;
HWND chart_window = nullptr;
HWND element_table_window = nullptr;
HWND element_search_edit = nullptr;
HBRUSH element_search_background = nullptr;
std::vector<std::size_t> chart_particle_indices;
std::vector<ChartSample> chart_samples;
std::size_t chart_next_sample = 0;
std::size_t chart_sample_count = 0;
bool chart_recording = false;
std::uint64_t chart_last_recorded_step = 0;
std::size_t chart_quantity = 0;
bool chart_difference_mode = false;
bool chart_selection_initialized = false;
bool molecule_tool_active = false;
bool bond_tool_active = false;
bool wall_tool_active = false;
bool wall_dragging = false;
bool saved_3d_camera = false;
double saved_3d_yaw = -pi / 4.0;
double saved_3d_pitch = pi / 3.0;
Vec3 wall_start{};
Vec3 wall_end{};
MolecularTool molecular_tool = MolecularTool::select;
std::vector<std::size_t> selected_particles;
std::vector<ScenarioStep> scenario_steps;
bool scenario_running = false;
double scenario_elapsed_seconds = 0.0;
std::size_t active_scenario_step = static_cast<std::size_t>(-1);
std::size_t square_lattice_count = 25;
int selected_element_number = 6;
int highlighted_element_number = 6;
bool square_lattice_dragging = false;
bool square_lattice_for_scenario = false;
double pending_lattice_duration = 1.0;
Vec3 square_lattice_start{};
Vec3 square_lattice_end{};
std::uint64_t pending_bond_molecule_id = 0;
bool simulation_running = false;
double simulation_time_accumulator = 0.0;
ULONGLONG last_simulation_tick = 0;
double molecule_placement_height = molecule_radius;
bool pointer_over_scene = false;
bool orbit_dragging = false;
bool pan_dragging = false;
bool molecule_dragging = false;
InteractionTool interaction_tool = InteractionTool::spring_drag;
std::size_t interacting_particle = static_cast<std::size_t>(-1);
std::uint64_t selected_molecule_id = 0;
Vec3 molecule_drag_offset{};
POINT pointer_position{};
POINT workspace_pointer_position{};
POINT drag_last_position{};
POINT force_drag_start_position{};

Vec3 operator+(Vec3 left, Vec3 right)
{
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right)
{
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 vector, double scalar)
{
    return Vec3{vector.x * scalar, vector.y * scalar, vector.z * scalar};
}

double dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x};
}

Vec3 normalized(Vec3 vector)
{
    const double length = std::sqrt(dot(vector, vector));
    return length > 1e-12 ? vector * (1.0 / length) : Vec3{};
}

COLORREF element_color(int atomic_number)
{
    const elements::Element* element = elements::find(atomic_number);
    if (element == nullptr)
    {
        return primary_text_color;
    }
    const unsigned int color = elements::category_color(element->category);
    return RGB((color >> 16U) & 0xFFU, (color >> 8U) & 0xFFU,
        color & 0xFFU);
}

double element_model_mass(const elements::Element& element)
{
    return element.atomic_mass / 12.011;
}

double element_model_sigma(const elements::Element& element)
{
    return (std::clamp)(element.covalent_radius_pm * 0.0075, 0.35, 1.8);
}

double element_model_epsilon(const elements::Element& element)
{
    switch (element.category)
    {
    case elements::Category::noble_gas: return 0.06;
    case elements::Category::alkali_metal:
    case elements::Category::alkaline_earth_metal:
    case elements::Category::transition_metal:
    case elements::Category::post_transition_metal: return 0.16;
    default: return 0.1;
    }
}

bool configure_particle_element(std::size_t particle_index,
    int atomic_number, double charge = 0.0, bool fixed = false)
{
    const elements::Element* element = elements::find(atomic_number);
    if (element == nullptr)
    {
        return false;
    }
    return simulation_engine.set_particle_properties(particle_index,
               element_model_mass(*element), charge,
               element_model_sigma(*element),
               element_model_epsilon(*element), fixed) &&
        simulation_engine.set_particle_element(particle_index, atomic_number);
}

int scene_width(const RECT& client_area)
{
    return (std::max)(1, static_cast<int>(client_area.right - client_area.left));
}

void apply_visual_style(VisualStyle style)
{
    const std::size_t index = static_cast<std::size_t>(style);
    if (index >= std::size(visual_palettes))
    {
        return;
    }
    visual_style = style;
    const VisualPalette& palette = visual_palettes[index];
    canvas_color = palette.canvas;
    toolbar_color = palette.toolbar;
    footer_color = palette.panel;
    border_color = palette.border;
    primary_text_color = palette.text;
    muted_text_color = palette.muted;
    x_axis_color = palette.accent;
    y_axis_color = palette.secondary;
    z_axis_color = palette.tertiary;
    surface_color = palette.surface;
    active_surface_color = palette.active_surface;
    if (element_table_window != nullptr)
    {
        HBRUSH themed_background = CreateSolidBrush(surface_color);
        if (themed_background == nullptr)
        {
            MessageBoxW(main_window,
                L"Не удалось обновить фон поля поиска элементов.",
                L"Настройки оформления", MB_OK | MB_ICONERROR);
        }
        else
        {
            if (element_search_background != nullptr)
            {
                DeleteObject(element_search_background);
            }
            element_search_background = themed_background;
        }
        InvalidateRect(element_table_window, nullptr, FALSE);
    }
    if (chart_window != nullptr)
    {
        InvalidateRect(chart_window, nullptr, FALSE);
    }
}

RECT scene_area(const RECT& client_area)
{
    const int top = client_area.top +
        (!separate_interface_windows && toolbar_visible ? toolbar_height : 0);
    const int bottom = (std::max)(top,
        static_cast<int>(client_area.bottom) -
            (!separate_interface_windows && footer_visible ? footer_height : 0));
    if (separate_interface_windows)
    {
        return RECT{client_area.left, top, client_area.right, bottom};
    }
    const int left = (std::min)(client_area.right,
        client_area.left + workspace_rail_width +
            (tools_panel_visible ? tools_panel_width : 0));
    return RECT{left, top, client_area.right, bottom};
}

RECT toolbar_area(const RECT& client_area)
{
    return RECT{client_area.left, client_area.top, client_area.right,
        client_area.top + toolbar_height};
}

RECT footer_area(const RECT& client_area)
{
    return RECT{client_area.left,
        client_area.bottom - footer_height,
        client_area.right, client_area.bottom};
}

RECT tools_panel_area(const RECT& client_area)
{
    if (!separate_interface_windows)
    {
        return RECT{
            client_area.left + workspace_rail_width,
            client_area.top + (toolbar_visible ? toolbar_height : 0),
            (std::min)(client_area.right,
                client_area.left + workspace_rail_width + tools_panel_width),
            client_area.bottom -
                (!separate_interface_windows && footer_visible
                    ? footer_height : 0)};
    }
    return RECT{
        client_area.right - tools_panel_width,
        client_area.top + (toolbar_visible ? toolbar_height : 0),
        client_area.right,
        client_area.bottom - (footer_visible ? footer_height : 0)};
}

RECT tools_panel_button_area(const RECT& client_area, int row)
{
    const RECT panel = tools_panel_area(client_area);
    const int top = panel.top + 48 + row * 36;
    return RECT{panel.left + 12, top, panel.right - 12, top + 30};
}

bool rectangles_intersect(const RECT& first, const RECT& second)
{
            return first.left < second.right && first.right > second.left &&
                first.top < second.bottom && first.bottom > second.top;
}

RECT molecule_tool_area(const RECT& client_area)
{
    return RECT{
        client_area.left + 260, client_area.top + 12,
        client_area.left + 304, client_area.top + 50};
}

RECT simulation_tool_area(const RECT& client_area)
{
        return RECT{
            client_area.left + 320, client_area.top + 12,
            client_area.left + 364, client_area.top + 50};
}

RECT time_flow_slider_area(const RECT& client_area)
{
    return RECT{client_area.left + 390, client_area.top + 8,
        client_area.left + 560, client_area.top + 54};
}

void set_time_flow_from_pointer(HWND window, POINT position)
{
    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT slider = time_flow_slider_area(client_area);
    const int left = slider.left + 8;
    const int right = slider.right - 8;
    const double fraction = static_cast<double>(
        (std::clamp)(static_cast<int>(position.x), left, right) - left) /
        static_cast<double>((std::max)(1, right - left));
    time_flow_factor = minimum_time_flow_factor +
        fraction * (maximum_time_flow_factor - minimum_time_flow_factor);
    InvalidateRect(window, &slider, FALSE);
    if (detached_interface_window != nullptr)
    {
        InvalidateRect(detached_interface_window, nullptr, FALSE);
    }
}

RECT graphics_tool_area(const RECT& client_area)
{
    return RECT{
        client_area.left + 580, client_area.top + 12,
        client_area.left + 624, client_area.top + 50};
}

RECT interaction_tool_area(const RECT& client_area)
{
        return RECT{
            client_area.left + interaction_tool_left,
            client_area.top + interaction_tool_top,
            client_area.left + interaction_tool_left + interaction_tool_width,
            client_area.top + interaction_tool_top + interaction_tool_height};
}

RECT auto_config_tool_area(const RECT& client_area)
{
            return RECT{
                client_area.left + auto_config_tool_left,
                client_area.top + auto_config_tool_top,
                client_area.left + auto_config_tool_left + auto_config_tool_width,
                client_area.top + auto_config_tool_top + auto_config_tool_height};
}

RECT restart_tool_area(const RECT& client_area)
{
            return RECT{
                client_area.left + restart_tool_left,
                client_area.top + restart_tool_top,
                client_area.left + restart_tool_left + restart_tool_width,
                client_area.top + restart_tool_top + restart_tool_height};
}

RECT workspace_category_area(const RECT& client_area, WorkspacePage page)
{
    const int page_index = static_cast<int>(page) - 1;
    const int top = client_area.top + toolbar_height + 22 + page_index * 52;
    return RECT{client_area.left + 10, top,
        client_area.left + workspace_rail_width - 10, top + 40};
}

RECT molecule_tool_area(const RECT& client_area);
RECT simulation_tool_area(const RECT& client_area);
RECT graphics_tool_area(const RECT& client_area);

int hovered_ui_target(const RECT& client_area, POINT position)
{
    const WorkspacePage pages[]{
        WorkspacePage::objects, WorkspacePage::thermodynamics,
        WorkspacePage::dynamics, WorkspacePage::quantum,
        WorkspacePage::settings, WorkspacePage::experiments};
    for (std::size_t index = 0; index < std::size(pages); ++index)
    {
        const RECT button = workspace_category_area(client_area, pages[index]);
        if (PtInRect(&button, position))
        {
            return static_cast<int>(index);
        }
    }
    const RECT buttons[]{
        molecule_tool_area(client_area),
        simulation_tool_area(client_area),
        graphics_tool_area(client_area)};
    for (std::size_t index = 0; index < std::size(buttons); ++index)
    {
        if (PtInRect(&buttons[index], position))
        {
            return 10 + static_cast<int>(index);
        }
    }
    return -1;
}

void fill_rectangle(HDC device_context, const RECT& area, COLORREF color);
void draw_line(HDC device_context, int x1, int y1, int x2, int y2,
    COLORREF color, int width = 1);
void draw_text(HDC device_context, const wchar_t* text, RECT area,
    COLORREF color, HFONT font, UINT alignment = DT_LEFT);

void draw_icon(HDC context, RECT area, int icon, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HPEN previous = static_cast<HPEN>(SelectObject(context, pen));
    HBRUSH previous_brush = static_cast<HBRUSH>(
        SelectObject(context, GetStockObject(HOLLOW_BRUSH)));
    const int cx = (area.left + area.right) / 2;
    const int cy = (area.top + area.bottom) / 2;
    switch (icon)
    {
    case 0:
        Ellipse(context, cx - 7, cy - 7, cx + 7, cy + 7);
        MoveToEx(context, cx - 12, cy, nullptr);
        LineTo(context, cx + 12, cy);
        MoveToEx(context, cx, cy - 12, nullptr);
        LineTo(context, cx, cy + 12);
        break;
    case 1:
        MoveToEx(context, cx, cy - 9, nullptr);
        LineTo(context, cx, cy + 4);
        Ellipse(context, cx - 5, cy + 2, cx + 5, cy + 12);
        Ellipse(context, cx - 3, cy - 12, cx + 3, cy - 6);
        break;
    case 2:
        MoveToEx(context, cx - 10, cy + 8, nullptr);
        LineTo(context, cx - 4, cy + 1);
        LineTo(context, cx + 1, cy + 5);
        LineTo(context, cx + 9, cy - 8);
        break;
    case 3:
        Ellipse(context, cx - 11, cy - 5, cx + 11, cy + 5);
        Ellipse(context, cx - 5, cy - 11, cx + 5, cy + 11);
        Ellipse(context, cx - 2, cy - 2, cx + 2, cy + 2);
        break;
    case 4:
        Ellipse(context, cx - 6, cy - 6, cx + 6, cy + 6);
        for (int angle = 0; angle < 8; ++angle)
        {
            const double radians = angle * pi / 4.0;
            MoveToEx(context,
                cx + static_cast<int>(std::cos(radians) * 8.0),
                cy + static_cast<int>(std::sin(radians) * 8.0), nullptr);
            LineTo(context,
                cx + static_cast<int>(std::cos(radians) * 12.0),
                cy + static_cast<int>(std::sin(radians) * 12.0));
        }
        break;
    case 5:
        if (simulation_running)
        {
            MoveToEx(context, cx - 5, cy - 8, nullptr);
            LineTo(context, cx - 5, cy + 8);
            MoveToEx(context, cx + 5, cy - 8, nullptr);
            LineTo(context, cx + 5, cy + 8);
        }
        else
        {
            POINT triangle[]{
                {cx - 6, cy - 9}, {cx + 9, cy}, {cx - 6, cy + 9}};
            Polygon(context, triangle, 3);
        }
        break;
    case 6:
        Ellipse(context, cx - 8, cy - 10, cx + 8, cy + 6);
        MoveToEx(context, cx - 6, cy + 6, nullptr);
        LineTo(context, cx - 3, cy + 11);
        LineTo(context, cx + 3, cy + 11);
        LineTo(context, cx + 6, cy + 6);
        MoveToEx(context, cx, cy - 15, nullptr);
        LineTo(context, cx, cy - 10);
        MoveToEx(context, cx - 12, cy - 5, nullptr);
        LineTo(context, cx - 8, cy - 3);
        MoveToEx(context, cx + 12, cy - 5, nullptr);
        LineTo(context, cx + 8, cy - 3);
        break;
    }
    SelectObject(context, previous_brush);
    SelectObject(context, previous);
    DeleteObject(pen);
}

const wchar_t* workspace_page_title(WorkspacePage page)
{
    switch (page)
    {
    case WorkspacePage::objects: return L"ОБЪЕКТЫ";
    case WorkspacePage::thermodynamics: return L"ТЕРМОДИНАМИКА";
    case WorkspacePage::dynamics: return L"ДИНАМИКА";
    case WorkspacePage::quantum: return L"КВАНТОВАЯ ДИНАМИКА";
    case WorkspacePage::settings: return L"НАСТРОЙКИ";
    case WorkspacePage::experiments: return L"ЛАБОРАТОРИЯ";
    default: return L"";
    }
}

const wchar_t* workspace_page_hint(WorkspacePage page)
{
    switch (page)
    {
    case WorkspacePage::objects: return L"Молекулы и инструменты";
    case WorkspacePage::thermodynamics: return L"Состояние системы";
    case WorkspacePage::dynamics: return L"Параметры симуляции";
    case WorkspacePage::quantum: return L"Квантовый модуль";
    case WorkspacePage::settings: return L"Оформление и окно";
    case WorkspacePage::experiments: return L"Инструменты опытов";
    default: return L"";
    }
}

void draw_workspace_rail(HDC context, const RECT& client_area)
{
    RECT rail{client_area.left, client_area.top + toolbar_height,
        client_area.left + workspace_rail_width, client_area.bottom};
    fill_rectangle(context, rail, toolbar_color);
    draw_line(context, rail.right - 1, rail.top,
        rail.right - 1, rail.bottom, border_color);
    const WorkspacePage pages[]{
        WorkspacePage::objects,
        WorkspacePage::thermodynamics,
        WorkspacePage::dynamics,
        WorkspacePage::quantum,
        WorkspacePage::settings,
        WorkspacePage::experiments};
    const wchar_t* labels[]{
        L"Объекты", L"Термодинамика", L"Динамика",
        L"Квантовая динамика", L"Настройки", L"Лаборатория"};
    for (std::size_t index = 0; index < std::size(pages); ++index)
    {
        const RECT button = workspace_category_area(client_area, pages[index]);
        const bool active = workspace_page == pages[index];
        const bool hovered = PtInRect(&button, workspace_pointer_position) != FALSE;
        if (active || hovered)
        {
            fill_rectangle(context, button,
                active ? active_surface_color : surface_color);
            draw_line(context, button.left, button.top + 6,
                button.left, button.bottom - 6,
                active ? x_axis_color : border_color, active ? 3 : 1);
        }
        draw_icon(context, button, index == 5 ? 6 : static_cast<int>(index),
            active ? x_axis_color : muted_text_color);
        if (hovered)
        {
            RECT tooltip{button.right + 8, button.top + 7,
                button.right + 168, button.bottom - 7};
            fill_rectangle(context, tooltip, surface_color);
            draw_text(context, labels[index], tooltip,
                muted_text_color, small_font);
        }
    }
}

bool create_overlay_fonts(int dpi = 96)
{
    title_font = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    small_font = CreateFontW(-MulDiv(10, dpi, 96), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    button_font = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    status_font = CreateFontW(-MulDiv(11, dpi, 96), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return title_font != nullptr && small_font != nullptr &&
        button_font != nullptr && status_font != nullptr;
}

void destroy_overlay_fonts()
{
    if (title_font != nullptr)
    {
        DeleteObject(title_font);
        title_font = nullptr;
    }
    if (small_font != nullptr)
    {
        DeleteObject(small_font);
        small_font = nullptr;
    }
    if (button_font != nullptr)
    {
        DeleteObject(button_font);
        button_font = nullptr;
    }
    if (status_font != nullptr)
    {
        DeleteObject(status_font);
        status_font = nullptr;
    }
}

void invalidate_simulation_ui(HWND window, bool toolbar)
{
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT scene = scene_area(client_area);
        InvalidateRect(window, &scene, FALSE);
        if (footer_visible)
        {
            const RECT footer = footer_area(client_area);
            InvalidateRect(window, &footer, FALSE);
        }
        if (toolbar && toolbar_visible)
        {
            const RECT molecule_button = molecule_tool_area(client_area);
            const RECT simulation_button = simulation_tool_area(client_area);
            const RECT interaction_button =
                interaction_tool_area(client_area);
            const RECT auto_button = auto_config_tool_area(client_area);
            const RECT restart_button = restart_tool_area(client_area);
            InvalidateRect(window, &molecule_button, FALSE);
            InvalidateRect(window, &simulation_button, FALSE);
            InvalidateRect(window, &interaction_button, FALSE);
            InvalidateRect(window, &auto_button, FALSE);
            InvalidateRect(window, &restart_button, FALSE);
        }
        if (tools_panel_visible)
        {
            const RECT tools = tools_panel_area(client_area);
            InvalidateRect(window, &tools, FALSE);
        }
        if (tools_panel_visible)
        {
            const RECT tools = tools_panel_area(client_area);
            InvalidateRect(window, &tools, FALSE);
        }
}

void fill_rectangle(HDC device_context, const RECT& area, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(device_context, &area, brush);
    DeleteObject(brush);
}

void draw_line(HDC device_context, int x1, int y1, int x2, int y2,
    COLORREF color, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, pen));
    MoveToEx(device_context, x1, y1, nullptr);
    LineTo(device_context, x2, y2);
    SelectObject(device_context, previous_pen);
    DeleteObject(pen);
}

void draw_text(HDC device_context, const wchar_t* text, RECT area,
    COLORREF color, HFONT font, UINT alignment)
{
    HFONT previous_font = static_cast<HFONT>(SelectObject(device_context, font));
    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, color);
    DrawTextW(device_context, text, -1, &area,
        alignment | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(device_context, previous_font);
}

void draw_filled_circle(HDC device_context, int x, int y, int radius,
    COLORREF fill, COLORREF outline)
{
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, pen));
    HBRUSH previous_brush = static_cast<HBRUSH>(SelectObject(device_context, brush));
    Ellipse(device_context, x - radius, y - radius, x + radius + 1, y + radius + 1);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

CameraBasis camera_basis()
{
    const double cos_pitch = std::cos(camera_state.pitch);
    const Vec3 offset{
        camera_state.distance * cos_pitch * std::cos(camera_state.yaw),
        camera_state.distance * cos_pitch * std::sin(camera_state.yaw),
        camera_state.distance * std::sin(camera_state.pitch)};
    const Vec3 eye = camera_state.target + offset;
    const Vec3 forward = normalized(camera_state.target - eye);
    const Vec3 world_up = std::abs(forward.z) > 0.98
        ? Vec3{0.0, 1.0, 0.0} : Vec3{0.0, 0.0, 1.0};
    const Vec3 right = normalized(cross(forward, world_up));
    const Vec3 up = normalized(cross(right, forward));
    return CameraBasis{eye, forward, right, up};
}

bool screen_to_plane(const RECT& client_area, POINT screen, double plane_z,
    Vec3& position)
{
    const RECT area = scene_area(client_area);
    if (area.right <= area.left || area.bottom <= area.top ||
        !PtInRect(&area, screen))
    {
        return false;
    }

    const CameraBasis basis = camera_basis();
    const double width = static_cast<double>(area.right - area.left);
    const double height = static_cast<double>(area.bottom - area.top);
    const double aspect = width / height;
    const double tan_half_fov = std::tan(field_of_view * pi / 360.0);
    const double ndc_x =
        (2.0 * (screen.x - area.left) / width - 1.0) * aspect * tan_half_fov;
    const double ndc_y =
        (1.0 - 2.0 * (screen.y - area.top) / height) * tan_half_fov;
    const Vec3 ray = normalized(basis.forward +
        basis.right * ndc_x + basis.up * ndc_y);

    if (std::abs(ray.z) < 1e-6)
    {
        return false;
    }

    const double distance = (plane_z - basis.eye.z) / ray.z;
    if (distance <= 0.0)
    {
        return false;
    }

    const Vec3 intersection = basis.eye + ray * distance;
    position = Vec3{intersection.x, intersection.y, plane_z};
    return true;
}

bool world_to_screen(const RECT& client_area, Vec3 position, POINT& screen,
    double& depth)
{
    const RECT area = scene_area(client_area);
    const CameraBasis basis = camera_basis();
    const Vec3 relative = position - basis.eye;
    depth = dot(relative, basis.forward);
    if (depth <= 0.1)
    {
        return false;
    }

    const double aspect = static_cast<double>(area.right - area.left) /
        static_cast<double>(area.bottom - area.top);
    const double tan_half_fov = std::tan(field_of_view * pi / 360.0);
    const double ndc_x = dot(relative, basis.right) /
        (depth * tan_half_fov * aspect);
    const double ndc_y = dot(relative, basis.up) /
        (depth * tan_half_fov);
    screen.x = static_cast<LONG>(std::lround(
        area.left + (ndc_x + 1.0) * (area.right - area.left) * 0.5));
    screen.y = static_cast<LONG>(std::lround(
        area.top + (1.0 - ndc_y) * (area.bottom - area.top) * 0.5));
    return true;
}

std::uint64_t pick_molecule(const RECT& client_area, POINT screen)
{
    const RECT area = scene_area(client_area);
    if (!PtInRect(&area, screen))
    {
        return 0;
    }

    std::uint64_t selected_id = 0;
    double nearest_depth = camera_maximum_distance;
    const double pixels_per_unit =
        static_cast<double>(area.bottom - area.top) /
        (2.0 * std::tan(field_of_view * pi / 360.0));
    const auto& particles = simulation_engine.particles();
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        const Vec3 position = particles[index].position;
        POINT center{};
        double depth = 0.0;
        if (!world_to_screen(client_area, position, center, depth))
        {
            continue;
        }
        const double screen_radius =
            molecule_radius * pixels_per_unit / depth + 5.0;
        const double dx = static_cast<double>(screen.x - center.x);
        const double dy = static_cast<double>(screen.y - center.y);
        if (dx * dx + dy * dy <= screen_radius * screen_radius &&
            depth < nearest_depth)
        {
            nearest_depth = depth;
            selected_id = static_cast<std::uint64_t>(index + 1);
        }
    }

    return selected_id;
}

void set_camera_viewport(const RECT& client_area)
{
    const RECT area = scene_area(client_area);
    const int height = static_cast<int>(client_area.bottom - client_area.top);
    glViewport(
        static_cast<GLint>(area.left),
        static_cast<GLint>(height - area.bottom),
        static_cast<GLsizei>(area.right - area.left),
        static_cast<GLsizei>(area.bottom - area.top));
}

void set_camera_matrices(const RECT& client_area)
{
    const RECT area = scene_area(client_area);
    const double width = static_cast<double>(area.right - area.left);
    const double height = (std::max)(1.0,
        static_cast<double>(area.bottom - area.top));
    const double near_plane = 0.1;
    const double far_plane = 1200.0;
    const double top = near_plane * std::tan(field_of_view * pi / 360.0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-top * width / height, top * width / height,
        -top, top, near_plane, far_plane);

    const CameraBasis basis = camera_basis();
    const GLdouble view_matrix[]{
        basis.right.x, basis.up.x, -basis.forward.x, 0.0,
        basis.right.y, basis.up.y, -basis.forward.y, 0.0,
        basis.right.z, basis.up.z, -basis.forward.z, 0.0,
        0.0, 0.0, 0.0, 1.0};
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMultMatrixd(view_matrix);
    glTranslated(-basis.eye.x, -basis.eye.y, -basis.eye.z);
}

double grid_extent()
{
    return (std::clamp)(camera_state.distance * 1.75, 30.0, 180.0);
}

void draw_grid()
{
    if (!grid_visible)
    {
        return;
    }

    const double extent = grid_extent();
    const double minimum_x = camera_state.target.x - extent;
    const double maximum_x = camera_state.target.x + extent;
    const double minimum_y = camera_state.target.y - extent;
    const double maximum_y = camera_state.target.y + extent;
    const int first_x = static_cast<int>(std::floor(minimum_x));
    const int last_x = static_cast<int>(std::ceil(maximum_x));
    const int first_y = static_cast<int>(std::floor(minimum_y));
    const int last_y = static_cast<int>(std::ceil(maximum_y));
    const int line_step =
        graphics_quality == GraphicsQuality::performance ? 2 : 1;

    glDisable(GL_LIGHTING);
    glLineWidth(1.0F);
    glBegin(GL_LINES);
    for (int x = first_x; x <= last_x; ++x)
    {
        if (x % line_step != 0)
        {
            continue;
        }
        const bool major = x % 5 == 0;
        const double shade = major ? 1.0 : 0.68;
        glColor4ub(
            static_cast<GLubyte>(GetRValue(border_color) * shade),
            static_cast<GLubyte>(GetGValue(border_color) * shade),
            static_cast<GLubyte>(GetBValue(border_color) * shade), 255);
        glVertex3d(x, minimum_y, 0.0);
        glVertex3d(x, maximum_y, 0.0);
    }
    for (int y = first_y; y <= last_y; ++y)
    {
        if (y % line_step != 0)
        {
            continue;
        }
        const bool major = y % 5 == 0;
        const double shade = major ? 1.0 : 0.68;
        glColor4ub(
            static_cast<GLubyte>(GetRValue(border_color) * shade),
            static_cast<GLubyte>(GetGValue(border_color) * shade),
            static_cast<GLubyte>(GetBValue(border_color) * shade), 255);
        glVertex3d(minimum_x, y, 0.0);
        glVertex3d(maximum_x, y, 0.0);
    }
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    glColor3ub(GetRValue(x_axis_color), GetGValue(x_axis_color),
        GetBValue(x_axis_color));
    glVertex3d(minimum_x, 0.0, 0.025);
    glVertex3d(maximum_x, 0.0, 0.025);
    glColor3ub(GetRValue(y_axis_color), GetGValue(y_axis_color),
        GetBValue(y_axis_color));
    glVertex3d(0.0, minimum_y, 0.025);
    glVertex3d(0.0, maximum_y, 0.025);
    glColor3ub(GetRValue(z_axis_color), GetGValue(z_axis_color),
        GetBValue(z_axis_color));
    if (!simulation_engine.config().two_dimensional)
    {
        glVertex3d(0.0, 0.0, 0.025);
        glVertex3d(0.0, 0.0, extent);
    }
    glEnd();
    glLineWidth(1.0F);
}

void draw_simulation_boundaries()
{
    const auto& config = simulation_engine.config();
    if (config.boundary_mode == simulation::BoundaryMode::none)
    {
        return;
    }

    const double low = config.boundary_min;
    const double high = config.boundary_max;
    const Vec3 corners[]{
        {low, low, low}, {high, low, low}, {high, high, low}, {low, high, low},
        {low, low, high}, {high, low, high}, {high, high, high}, {low, high, high}};
    constexpr int edges[][2]{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    glDisable(GL_LIGHTING);
    glLineWidth(1.5F);
    const COLORREF boundary_color =
        config.boundary_mode == simulation::BoundaryMode::soft
        ? z_axis_color : y_axis_color;
    glColor3ub(GetRValue(boundary_color), GetGValue(boundary_color),
        GetBValue(boundary_color));
    glBegin(GL_LINES);
    for (const auto& edge : edges)
    {
        const Vec3 first = corners[edge[0]];
        const Vec3 second = corners[edge[1]];
        glVertex3d(first.x, first.y, first.z);
        glVertex3d(second.x, second.y, second.z);
    }
    glEnd();
    glLineWidth(1.0F);
}

void draw_walls()
{
    const bool two_dimensional =
        simulation_engine.config().two_dimensional;
    glDisable(GL_LIGHTING);
    glLineWidth(3.0F);
    glColor3ub(GetRValue(x_axis_color), GetGValue(x_axis_color),
        GetBValue(x_axis_color));
    glBegin(GL_LINES);
    for (const simulation::Wall& wall : simulation_engine.walls())
    {
        if (wall.plane == two_dimensional)
        {
            continue;
        }
        if (wall.plane)
        {
            glVertex3d(wall.start.x, wall.start.y, wall.start.z);
            glVertex3d(wall.end.x, wall.start.y, wall.start.z);
            glVertex3d(wall.end.x, wall.start.y, wall.start.z);
            glVertex3d(wall.end.x, wall.end.y, wall.start.z);
            glVertex3d(wall.end.x, wall.end.y, wall.start.z);
            glVertex3d(wall.start.x, wall.end.y, wall.start.z);
            glVertex3d(wall.start.x, wall.end.y, wall.start.z);
            glVertex3d(wall.start.x, wall.start.y, wall.start.z);
        }
        else
        {
            glVertex3d(wall.start.x, wall.start.y, wall.start.z + 0.03);
            glVertex3d(wall.end.x, wall.end.y, wall.end.z + 0.03);
        }
    }
    glEnd();
    if (wall_tool_active && wall_dragging)
    {
        glColor3ub(GetRValue(z_axis_color), GetGValue(z_axis_color),
            GetBValue(z_axis_color));
        glBegin(GL_LINES);
        if (two_dimensional)
        {
            glVertex3d(wall_start.x, wall_start.y, 0.04);
            glVertex3d(wall_end.x, wall_end.y, 0.04);
        }
        else
        {
            glVertex3d(wall_start.x, wall_start.y, wall_start.z);
            glVertex3d(wall_end.x, wall_start.y, wall_start.z);
            glVertex3d(wall_end.x, wall_start.y, wall_start.z);
            glVertex3d(wall_end.x, wall_end.y, wall_start.z);
            glVertex3d(wall_end.x, wall_end.y, wall_start.z);
            glVertex3d(wall_start.x, wall_end.y, wall_start.z);
            glVertex3d(wall_start.x, wall_end.y, wall_start.z);
            glVertex3d(wall_start.x, wall_start.y, wall_start.z);
        }
        glEnd();
    }
    glLineWidth(1.0F);
}

void draw_sphere(const Vec3& center, double radius, bool preview = false,
    bool selected = false, double charge = 0.0, int atomic_number = 6)
{
    int latitude_steps = 12;
    int longitude_steps = 18;
    if (graphics_quality == GraphicsQuality::performance)
    {
        latitude_steps = 8;
        longitude_steps = 12;
    }
    else if (graphics_quality == GraphicsQuality::quality)
    {
        latitude_steps = 20;
        longitude_steps = 30;
    }
    if (preview)
    {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4ub(GetRValue(x_axis_color), GetGValue(x_axis_color),
            GetBValue(x_axis_color), 90);
    }
    else
    {
        glEnable(GL_LIGHTING);
        if (selected)
        {
            glColor3ub(GetRValue(x_axis_color), GetGValue(x_axis_color),
                GetBValue(x_axis_color));
        }
        else
        {
            const COLORREF color = charge > 0.0
                ? RGB(225, 132, 110)
                : charge < 0.0 ? RGB(151, 154, 232)
                               : element_color(atomic_number);
            glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
        }
    }

    for (int latitude = 0; latitude < latitude_steps; ++latitude)
    {
        const double low_phi = -pi / 2.0 + pi * latitude / latitude_steps;
        const double high_phi = -pi / 2.0 + pi * (latitude + 1) / latitude_steps;
        glBegin(GL_QUAD_STRIP);
        for (int longitude = 0; longitude <= longitude_steps; ++longitude)
        {
            const double theta = 2.0 * pi * longitude / longitude_steps;
            for (const double phi : {low_phi, high_phi})
            {
                const Vec3 normal{
                    std::cos(phi) * std::cos(theta),
                    std::cos(phi) * std::sin(theta),
                    std::sin(phi)};
                glNormal3d(normal.x, normal.y, normal.z);
                glVertex3d(center.x + radius * normal.x,
                    center.y + radius * normal.y,
                    center.z + radius * normal.z);
            }
        }
        glEnd();
    }

    if (preview)
    {
        glDisable(GL_BLEND);
    }
}

void draw_molecules(const RECT& client_area)
{
    const double cull_distance = grid_extent() + camera_state.distance;
    const auto& particles = simulation_engine.particles();
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        const Vec3 position = particles[index].position;
        const Vec3 offset = position - camera_state.target;
        if (dot(offset, offset) >
            (cull_distance + molecule_radius) *
                (cull_distance + molecule_radius))
        {
            continue;
        }
        const bool selected =
            static_cast<std::uint64_t>(index + 1) == selected_molecule_id ||
            std::find(selected_particles.begin(), selected_particles.end(),
                index) != selected_particles.end();
        draw_sphere(position, molecule_radius, false, selected,
            particles[index].charge, particles[index].atomic_number);
    }

    Vec3 preview_position{};
    const double preview_z = simulation_engine.config().two_dimensional
        ? 0.0 : molecule_placement_height;
    if (molecule_tool_active && pointer_over_scene &&
        screen_to_plane(client_area, pointer_position,
            preview_z, preview_position))
    {
        draw_sphere(
            Vec3{preview_position.x, preview_position.y, preview_z},
            molecule_radius, true, false, 0.0, selected_element_number);
    }
}

void draw_square_lattice_preview()
{
    if (!square_lattice_dragging)
    {
        return;
    }
    glDisable(GL_LIGHTING);
    glLineWidth(2.0F);
    glColor3ub(GetRValue(x_axis_color), GetGValue(x_axis_color),
        GetBValue(x_axis_color));
    const double dx = square_lattice_end.x - square_lattice_start.x;
    const double dy = square_lattice_end.y - square_lattice_start.y;
    const double side = (std::max)(std::abs(dx), std::abs(dy));
    const double direction_x = dx < 0.0 ? -1.0 : 1.0;
    const double direction_y = dy < 0.0 ? -1.0 : 1.0;
    const double end_x = square_lattice_start.x + direction_x * side;
    const double end_y = square_lattice_start.y + direction_y * side;
    glBegin(GL_LINE_LOOP);
    glVertex3d(square_lattice_start.x, square_lattice_start.y,
        square_lattice_start.z);
    glVertex3d(end_x, square_lattice_start.y,
        square_lattice_start.z);
    glVertex3d(end_x, end_y,
        square_lattice_start.z);
    glVertex3d(square_lattice_start.x, end_y,
        square_lattice_start.z);
    glEnd();

    const std::size_t columns = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<double>(square_lattice_count))));
    const std::size_t rows =
        (square_lattice_count + columns - 1) / columns;
    const double spacing = side /
        static_cast<double>((std::max)(columns, rows));
    const double center_x = (square_lattice_start.x + end_x) * 0.5;
    const double center_y = (square_lattice_start.y + end_y) * 0.5;
    const double start_x = center_x -
        spacing * static_cast<double>(columns - 1) * 0.5;
    const double start_y = center_y -
        spacing * static_cast<double>(rows - 1) * 0.5;
    glPointSize(5.0F);
    glBegin(GL_POINTS);
    for (std::size_t index = 0; index < square_lattice_count; ++index)
    {
        const std::size_t column = index % columns;
        const std::size_t row = index / columns;
        glVertex3d(start_x + static_cast<double>(column) * spacing,
            start_y + static_cast<double>(row) * spacing,
            square_lattice_start.z);
    }
    glEnd();
    glPointSize(1.0F);
    glLineWidth(1.0F);
}

void draw_bonds()
{
    glDisable(GL_LIGHTING);
    glLineWidth(2.0F);
    glColor3ub(GetRValue(z_axis_color), GetGValue(z_axis_color),
        GetBValue(z_axis_color));
    glBegin(GL_LINES);
    const auto& particles = simulation_engine.particles();
    for (const simulation::Bond& bond : simulation_engine.bonds())
    {
        if (bond.first >= particles.size() || bond.second >= particles.size())
        {
            continue;
        }
        const Vec3 first = particles[bond.first].position;
        const Vec3 second = particles[bond.second].position;
        glVertex3d(first.x, first.y, first.z);
        glVertex3d(second.x, second.y, second.z);
    }
    glEnd();
    glLineWidth(1.0F);
}

void draw_interaction_force()
{
    if (!molecule_dragging ||
        interacting_particle >= simulation_engine.particles().size())
    {
        return;
    }
    const simulation::Particle& particle =
        simulation_engine.particles()[interacting_particle];
    if (dot(particle.external_force, particle.external_force) < 1e-12)
    {
        return;
    }
    const Vec3 endpoint =
        particle.position + particle.external_force * 0.05;
    glDisable(GL_LIGHTING);
    glLineWidth(3.0F);
    glColor3ub(GetRValue(z_axis_color), GetGValue(z_axis_color),
        GetBValue(z_axis_color));
    glBegin(GL_LINES);
    glVertex3d(particle.position.x, particle.position.y, particle.position.z);
    glVertex3d(endpoint.x, endpoint.y, endpoint.z);
    glEnd();
    glLineWidth(1.0F);
}

void render_scene(const RECT& client_area)
{
    const RECT area = scene_area(client_area);
    const int width = scene_width(client_area);
    const int height = (std::max)(1, static_cast<int>(client_area.bottom));

    glDrawBuffer(GL_BACK);
    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(area.left, height - area.bottom,
        area.right - area.left, area.bottom - area.top);
    glClearColor(GetRValue(canvas_color) / 255.0F,
        GetGValue(canvas_color) / 255.0F,
        GetBValue(canvas_color) / 255.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    set_camera_viewport(client_area);
    set_camera_matrices(client_area);

    const GLfloat ambient[]{0.23F, 0.25F, 0.29F, 1.0F};
    const GLfloat diffuse[]{0.85F, 0.91F, 1.0F, 1.0F};
    const GLfloat light_position[]{-12.0F, -10.0F, 24.0F, 1.0F};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_NORMALIZE);
    if (graphics_quality == GraphicsQuality::performance)
    {
        glDisable(GL_LINE_SMOOTH);
    }
    else
    {
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT,
            graphics_quality == GraphicsQuality::quality
                ? GL_NICEST : GL_DONT_CARE);
    }

    draw_grid();
    draw_simulation_boundaries();
    draw_walls();
    draw_bonds();
    draw_molecules(client_area);
    draw_square_lattice_preview();
    draw_interaction_force();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

void draw_toolbar(HDC device_context, const RECT& client_area, HFONT title_font,
    HFONT small_font, HFONT status_font)
{
    const RECT toolbar = toolbar_area(client_area);
    fill_rectangle(device_context, toolbar, toolbar_color);
    draw_line(device_context, client_area.left, toolbar.bottom - 1,
        client_area.right, toolbar.bottom - 1, border_color);

    const int brand_center_y = toolbar.top + toolbar_height / 2;
    draw_filled_circle(device_context, workspace_rail_width / 2,
        brand_center_y, 8, active_surface_color, x_axis_color);
    RECT title_area{workspace_rail_width + 16, toolbar.top + 8,
        workspace_rail_width + 188, toolbar.top + 34};
    draw_text(device_context, L"BONDERIUM", title_area,
        primary_text_color, title_font);
    RECT subtitle_area{workspace_rail_width + 17, toolbar.top + 33,
        workspace_rail_width + 190, toolbar.top + 52};
    draw_text(device_context,
        simulation_engine.config().two_dimensional
            ? L"2D SIMULATION" : L"3D SIMULATION",
        subtitle_area, muted_text_color, small_font);

    const RECT molecule_button = molecule_tool_area(client_area);
    fill_rectangle(device_context, molecule_button,
        molecule_tool_active ? active_surface_color : surface_color);
    draw_icon(device_context, molecule_button, 0,
        molecule_tool_active ? x_axis_color : primary_text_color);

    const RECT simulation_button = simulation_tool_area(client_area);
    fill_rectangle(device_context, simulation_button,
        simulation_running ? active_surface_color : surface_color);
    draw_icon(device_context, simulation_button, 5,
        simulation_running ? x_axis_color : primary_text_color);

    const RECT time_slider = time_flow_slider_area(client_area);
    wchar_t time_flow_label[24]{};
    std::swprintf(time_flow_label,
        sizeof(time_flow_label) / sizeof(time_flow_label[0]),
        L"ВРЕМЯ  %.2fx", time_flow_factor);
    draw_text(device_context, time_flow_label,
        RECT{time_slider.left, time_slider.top,
            time_slider.right, time_slider.top + 19},
        muted_text_color, small_font, DT_CENTER);
    const int slider_left = time_slider.left + 8;
    const int slider_right = time_slider.right - 8;
    const int slider_y = time_slider.top + 31;
    draw_line(device_context, slider_left, slider_y, slider_right,
        slider_y, border_color, 3);
    const double normalized_speed =
        (time_flow_factor - minimum_time_flow_factor) /
        (maximum_time_flow_factor - minimum_time_flow_factor);
    const int slider_x = slider_left + static_cast<int>(
        normalized_speed * (slider_right - slider_left));
    draw_line(device_context, slider_left, slider_y, slider_x,
        slider_y, x_axis_color, 3);
    draw_filled_circle(device_context, slider_x, slider_y, 5,
        primary_text_color, x_axis_color);

    const RECT graphics_button = graphics_tool_area(client_area);
    fill_rectangle(device_context, graphics_button, surface_color);
    draw_icon(device_context, graphics_button, 4, primary_text_color);

    wchar_t molecule_count[32]{};
    std::swprintf(molecule_count, std::size(molecule_count), L"%llu",
        static_cast<unsigned long long>(simulation_engine.particles().size()));
    RECT count_area{client_area.right - 104, toolbar.top + 13,
        client_area.right - 18, toolbar.top + 49};
    draw_text(device_context, molecule_count, count_area,
        primary_text_color, status_font, DT_RIGHT);

    const RECT hoverable[]{
        molecule_button, simulation_button, graphics_button};
    const wchar_t* tooltips[]{
        L"Создать молекулу", L"Запустить или остановить симуляцию",
        L"Настройки и оформление"};
    for (std::size_t index = 0; index < std::size(hoverable); ++index)
    {
        if (PtInRect(&hoverable[index], workspace_pointer_position))
        {
            RECT tooltip{hoverable[index].left - 10,
                hoverable[index].bottom + 2,
                hoverable[index].right + 118,
                hoverable[index].bottom + 24};
            fill_rectangle(device_context, tooltip, surface_color);
            draw_text(device_context, tooltips[index], tooltip,
                muted_text_color, small_font);
        }
    }
}

void draw_footer(HDC device_context, const RECT& client_area, HFONT small_font,
    HFONT status_font)
{
    const RECT footer = footer_area(client_area);
    fill_rectangle(device_context, footer, footer_color);
    draw_line(device_context, client_area.left, footer.top,
        client_area.right, footer.top, border_color);

    wchar_t molecule_value[24]{};
    wchar_t charge_value[24]{};
    wchar_t temperature_value[24]{};
    wchar_t pressure_value[24]{};
    wchar_t density_value[24]{};
    wchar_t kinetic_value[24]{};
    wchar_t potential_value[24]{};
    wchar_t total_energy_value[24]{};
    wchar_t fps_value[24]{};
    wchar_t steps_value[24]{};
    wchar_t frame_time_value[24]{};
    wchar_t camera_value[32]{};
    wchar_t uptime_value[24]{};
    wchar_t memory_value[32]{};
    const simulation::Metrics& metrics = simulation_engine.metrics();
    std::swprintf(molecule_value, sizeof(molecule_value) / sizeof(molecule_value[0]),
        L"%llu", static_cast<unsigned long long>(
            simulation_engine.particles().size()));
    if (selected_molecule_id > 0 &&
        selected_molecule_id <= simulation_engine.particles().size())
    {
        const std::size_t selected_index =
            static_cast<std::size_t>(selected_molecule_id - 1);
        if (selected_index < simulation_engine.particles().size())
        {
            std::swprintf(charge_value,
                sizeof(charge_value) / sizeof(charge_value[0]),
                L"%+.1f", simulation_engine.particles()[selected_index].charge);
        }
        else
        {
            std::swprintf(charge_value,
                sizeof(charge_value) / sizeof(charge_value[0]), L"—");
        }
    }
    else
    {
        std::swprintf(charge_value,
            sizeof(charge_value) / sizeof(charge_value[0]), L"—");
    }
    std::swprintf(temperature_value,
        sizeof(temperature_value) / sizeof(temperature_value[0]),
        L"%.3f", metrics.temperature);
    if (metrics.pressure_available)
    {
        std::swprintf(pressure_value,
            sizeof(pressure_value) / sizeof(pressure_value[0]),
            L"%.3f", metrics.pressure);
        std::swprintf(density_value,
            sizeof(density_value) / sizeof(density_value[0]),
            L"%.3f", metrics.number_density);
    }
    else
    {
        std::swprintf(pressure_value,
            sizeof(pressure_value) / sizeof(pressure_value[0]), L"—");
        std::swprintf(density_value,
            sizeof(density_value) / sizeof(density_value[0]), L"—");
    }
    std::swprintf(kinetic_value,
        sizeof(kinetic_value) / sizeof(kinetic_value[0]),
        L"%.3f", metrics.kinetic_energy);
    std::swprintf(potential_value,
        sizeof(potential_value) / sizeof(potential_value[0]),
        L"%.3f", metrics.potential_energy);
    std::swprintf(total_energy_value,
        sizeof(total_energy_value) / sizeof(total_energy_value[0]),
        L"%.3f", metrics.total_energy);
    std::swprintf(fps_value, sizeof(fps_value) / sizeof(fps_value[0]),
        L"%.0f", telemetry.frames_per_second);
    std::swprintf(steps_value, sizeof(steps_value) / sizeof(steps_value[0]),
        L"%.0f", telemetry.simulation_steps_per_second);
    if (telemetry.has_frame_sample)
    {
        std::swprintf(frame_time_value,
            sizeof(frame_time_value) / sizeof(frame_time_value[0]),
            L"%.1f ms", telemetry.frame_time_ms);
    }
    else
    {
        std::swprintf(frame_time_value,
            sizeof(frame_time_value) / sizeof(frame_time_value[0]), L"—");
    }
    const double camera_speed = std::sqrt(dot(camera_state.velocity, camera_state.velocity));
    std::swprintf(camera_value, sizeof(camera_value) / sizeof(camera_value[0]),
        L"%.1f u/s", camera_speed);
    if (process_memory_available)
    {
        if (ram_budget_bytes == 0)
        {
            std::swprintf(memory_value,
                sizeof(memory_value) / sizeof(memory_value[0]),
                L"%.0f MB", process_private_bytes / (1024.0 * 1024.0));
        }
        else
        {
            std::swprintf(memory_value,
                sizeof(memory_value) / sizeof(memory_value[0]),
                L"%.0f / %.0f MB",
                process_private_bytes / (1024.0 * 1024.0),
                ram_budget_bytes / (1024.0 * 1024.0));
        }
    }
    else
    {
        std::swprintf(memory_value,
            sizeof(memory_value) / sizeof(memory_value[0]), L"Unavailable");
    }
    const ULONGLONG uptime_seconds =
        telemetry.started_at == 0 ? 0 : (GetTickCount64() - telemetry.started_at) / 1000;
    if (uptime_seconds >= 3600)
    {
        std::swprintf(uptime_value,
            sizeof(uptime_value) / sizeof(uptime_value[0]), L"%llu:%02llu:%02llu",
            static_cast<unsigned long long>(uptime_seconds / 3600),
            static_cast<unsigned long long>((uptime_seconds / 60) % 60),
            static_cast<unsigned long long>(uptime_seconds % 60));
    }
    else
    {
        std::swprintf(uptime_value,
            sizeof(uptime_value) / sizeof(uptime_value[0]), L"%02llu:%02llu",
            static_cast<unsigned long long>(uptime_seconds / 60),
            static_cast<unsigned long long>(uptime_seconds % 60));
    }

    struct Metric
    {
        const wchar_t* label;
        const wchar_t* value;
    };
    const Metric thermodynamic_metrics[]{
        {L"MOLECULES", molecule_value},
        {L"TEMPERATURE", temperature_value},
        {L"PRESSURE", pressure_value},
        {L"NUMBER DENSITY", density_value},
        {L"KINETIC ENERGY", kinetic_value},
        {L"POTENTIAL ENERGY", potential_value},
        {L"TOTAL ENERGY", total_energy_value}};
    constexpr int thermodynamic_count = static_cast<int>(
        sizeof(thermodynamic_metrics) / sizeof(thermodynamic_metrics[0]));
    const int thermodynamic_width =
        scene_width(client_area) / thermodynamic_count;
    draw_text(device_context, L"THERMODYNAMICS",
        RECT{16, footer.top + 3, client_area.right - 16, footer.top + 17},
        muted_text_color, small_font);
    draw_line(device_context, client_area.right - 24, footer.top + 7,
        client_area.right - 14, footer.top + 17, muted_text_color);
    draw_line(device_context, client_area.right - 14, footer.top + 7,
        client_area.right - 24, footer.top + 17, muted_text_color);
    for (int index = 0; index < thermodynamic_count; ++index)
    {
        const int left = client_area.left + index * thermodynamic_width;
        const int right = index == thermodynamic_count - 1
            ? client_area.right
            : left + thermodynamic_width;
        RECT label_area{left + 4, footer.top + 17, right - 4, footer.top + 31};
        RECT value_area{left + 4, footer.top + 30, right - 4, footer.top + 48};
        draw_text(device_context, thermodynamic_metrics[index].label, label_area,
            muted_text_color, small_font, DT_CENTER);
        draw_text(device_context, thermodynamic_metrics[index].value,
            value_area, index == 1 ? x_axis_color : primary_text_color,
            status_font, DT_CENTER);
        if (index > 0)
        {
            draw_line(device_context, left, footer.top + 20,
                left, footer.top + 45, border_color);
        }
    }

    draw_line(device_context, client_area.left, footer.top + 50,
        client_area.right, footer.top + 50, border_color);
    draw_text(device_context, L"SIMULATION",
        RECT{16, footer.top + 52, client_area.right - 16, footer.top + 66},
        muted_text_color, small_font);
    const Metric runtime_metrics[]{
        {L"FPS", fps_value},
        {L"STEPS / SEC", steps_value},
        {L"CHARGE", charge_value},
        {L"FRAME TIME", frame_time_value},
        {L"CAMERA SPEED", camera_value},
        {L"UPTIME", uptime_value},
        {L"RAM / BUDGET", memory_value}};
    constexpr int runtime_count = static_cast<int>(
        sizeof(runtime_metrics) / sizeof(runtime_metrics[0]));
    const int runtime_width = scene_width(client_area) / runtime_count;
    for (int index = 0; index < runtime_count; ++index)
    {
        const int left = client_area.left + index * runtime_width;
        const int right = index == runtime_count - 1
            ? client_area.right
            : left + runtime_width;
        RECT label_area{left + 4, footer.top + 66, right - 4, footer.top + 79};
        RECT value_area{left + 4, footer.top + 78, right - 4, footer.top + 95};
        draw_text(device_context, runtime_metrics[index].label, label_area,
            muted_text_color, small_font, DT_CENTER);
        draw_text(device_context, runtime_metrics[index].value, value_area,
            index == 0 ? x_axis_color : primary_text_color,
            status_font, DT_CENTER);
        if (index > 0)
        {
            draw_line(device_context, left, footer.top + 69,
                left, footer.top + 92, border_color);
        }
    }

    draw_line(device_context, client_area.left, footer.top + 97,
        client_area.right, footer.top + 97, border_color);
    RECT instructions{16, footer.top + 98,
        client_area.right - 390, footer.bottom - 1};
    const wchar_t* hint = molecule_tool_active
        ? L"CLICK PLACE   SHIFT+WHEEL HEIGHT   ESC CANCEL"
        : bond_tool_active
        ? L"CLICK TWO MOLECULES TO BOND   ESC CANCEL"
        : molecule_dragging &&
            interaction_tool == InteractionTool::directional_force
        ? L"DRAG TO SET FORCE VECTOR   RELEASE TO STOP"
        : molecule_dragging
        ? L"SPRING FORCE ACTIVE   RELEASE TO STOP"
        : interaction_tool != InteractionTool::select && !simulation_running
        ? L"START SIMULATION TO APPLY MOLECULE FORCES"
        : interaction_tool == InteractionTool::directional_force
        ? L"SELECT MOLECULE   HOLD+DRAG TO APPLY FORCE"
        : interaction_tool == InteractionTool::spring_drag
        ? L"SELECT MOLECULE   DRAG WITH SPRING FORCE"
        : L"F1 HELP   F2 PANELS   WASD/QE MOVE   RMB ORBIT   MMB PAN   WHEEL ZOOM   SPACE SIM";
    draw_text(device_context, hint, instructions, muted_text_color, small_font);

    wchar_t coordinates[3][32]{};
    std::swprintf(coordinates[0], sizeof(coordinates[0]) / sizeof(coordinates[0][0]),
        L"X: %.2f", camera_state.target.x);
    std::swprintf(coordinates[1], sizeof(coordinates[1]) / sizeof(coordinates[1][0]),
        L"Y: %.2f", camera_state.target.y);
    std::swprintf(coordinates[2], sizeof(coordinates[2]) / sizeof(coordinates[2][0]),
        L"Z: %.2f", camera_state.target.z);
    RECT status_area{client_area.right - 378, footer.top + 98,
        client_area.right - 16, footer.bottom - 1};
    const int coordinate_width =
        (status_area.right - status_area.left) / 3;
    const COLORREF coordinate_colors[]{x_axis_color, y_axis_color, z_axis_color};
    for (int index = 0; index < 3; ++index)
    {
        RECT coordinate_area{
            status_area.left + index * coordinate_width,
            status_area.top,
            index == 2 ? status_area.right
                       : status_area.left + (index + 1) * coordinate_width,
            status_area.bottom};
        draw_text(device_context, coordinates[index], coordinate_area,
            coordinate_colors[index], status_font, DT_CENTER);
    }
}

void draw_toolbar_overlay(HDC device_context, const RECT& client_area)
{
    if (title_font != nullptr && small_font != nullptr &&
        button_font != nullptr && status_font != nullptr)
    {
        draw_toolbar(device_context, client_area, title_font, small_font,
            status_font);
    }
}

void draw_footer_overlay(HDC device_context, const RECT& client_area)
{
    if (small_font != nullptr && status_font != nullptr)
    {
        draw_footer(device_context, client_area, small_font, status_font);
    }
}

void draw_tools_panel_overlay(HDC device_context, const RECT& client_area)
{
    if (!tools_panel_visible)
    {
        return;
    }
    const RECT panel = tools_panel_area(client_area);
    fill_rectangle(device_context, panel, toolbar_color);
    draw_line(device_context, panel.right - 1, panel.top,
        panel.right - 1, panel.bottom, border_color);
    draw_line(device_context, panel.left, panel.top + 38,
        panel.right, panel.top + 38, border_color);
    draw_text(device_context,
        workspace_page == WorkspacePage::closed
            ? L"TOOLS" : workspace_page_title(workspace_page),
        RECT{panel.left + 16, panel.top + 10, panel.right - 48, panel.top + 30},
        primary_text_color, button_font);
    if (workspace_page != WorkspacePage::closed)
    {
        draw_text(device_context, workspace_page_hint(workspace_page),
            RECT{panel.left + 16, panel.top + 27,
                panel.right - 40, panel.top + 38},
            muted_text_color, small_font);
    }
    draw_line(device_context, panel.right - 25, panel.top + 13,
        panel.right - 13, panel.top + 25, muted_text_color);
    draw_line(device_context, panel.right - 13, panel.top + 13,
        panel.right - 25, panel.top + 25, muted_text_color);

    const auto draw_tool_button = [device_context](const RECT& area,
                                      const wchar_t* label, bool active) {
        fill_rectangle(device_context, area,
            active ? active_surface_color : surface_color);
        draw_line(device_context, area.left, area.top,
            area.right, area.top, border_color);
        draw_line(device_context, area.left, area.bottom - 1,
            area.right, area.bottom - 1, border_color);
        draw_line(device_context, area.left, area.top,
            area.left, area.bottom, border_color);
        draw_line(device_context, area.right - 1, area.top,
            area.right - 1, area.bottom, border_color);
        draw_text(device_context, label,
            RECT{area.left + 5, area.top, area.right - 5, area.bottom},
            active ? x_axis_color : primary_text_color,
            small_font, DT_CENTER);
    };
    const wchar_t* style_names[]{
        L"ГЛУБОКИЙ КОСМОС", L"АРКТИКА", L"ЛЕС", L"ЯНТАРЬ", L"ОРХИДЕЯ"};
    if (workspace_page == WorkspacePage::thermodynamics)
    {
        const auto& metrics = simulation_engine.metrics();
        wchar_t values[7][32]{};
        std::swprintf(values[0], std::size(values[0]), L"%llu",
            static_cast<unsigned long long>(
                simulation_engine.particles().size()));
        std::swprintf(values[1], std::size(values[1]), L"%.4f",
            metrics.temperature);
        std::swprintf(values[2], std::size(values[2]), L"%.4f",
            metrics.kinetic_energy);
        std::swprintf(values[3], std::size(values[3]), L"%.4f",
            metrics.potential_energy);
        std::swprintf(values[4], std::size(values[4]), L"%.4f",
            metrics.total_energy);
        std::swprintf(values[5], std::size(values[5]), L"%.4f",
            metrics.pressure);
        std::swprintf(values[6], std::size(values[6]), L"%.4f",
            metrics.number_density);
        const wchar_t* labels[]{
            L"МОЛЕКУЛЫ", L"ТЕМПЕРАТУРА", L"КИНЕТИЧЕСКАЯ ЭНЕРГИЯ",
            L"ПОТЕНЦИАЛЬНАЯ ЭНЕРГИЯ", L"ПОЛНАЯ ЭНЕРГИЯ",
            L"ДАВЛЕНИЕ", L"ПЛОТНОСТЬ"};
        for (int index = 0; index < 7; ++index)
        {
            const int top = panel.top + 52 + index * 48;
            draw_text(device_context, labels[index],
                RECT{panel.left + 14, top, panel.right - 14, top + 16},
                muted_text_color, small_font);
            draw_text(device_context,
                index >= 5 && !metrics.pressure_available
                    ? L"—" : values[index],
                RECT{panel.left + 14, top + 15,
                    panel.right - 14, top + 39},
                index == 1 ? x_axis_color : primary_text_color,
                status_font);
            if (index < 6)
            {
                draw_line(device_context, panel.left + 12, top + 44,
                    panel.right - 12, top + 44, border_color);
            }
        }
        return;
    }
    if (workspace_page == WorkspacePage::quantum)
    {
        draw_icon(device_context,
            RECT{panel.left + 75, panel.top + 78,
                panel.right - 75, panel.top + 128}, 3, x_axis_color);
        draw_text(device_context, L"КВАНТОВЫЙ РЕЖИМ",
            RECT{panel.left + 12, panel.top + 146,
                panel.right - 12, panel.top + 170},
            primary_text_color, button_font, DT_CENTER);
        draw_text(device_context, L"Раздел подготовлен для будущих "
                L"инструментов. Классическая симуляция доступна в панели "
                L"Динамика.",
            RECT{panel.left + 16, panel.top + 184,
                panel.right - 16, panel.top + 254},
            muted_text_color, small_font, DT_CENTER | DT_WORDBREAK);
        return;
    }
    if (workspace_page == WorkspacePage::dynamics)
    {
        const wchar_t* labels[]{
            L"ИНСТРУМЕНТ ВЗАИМОДЕЙСТВИЯ",
            L"ТЕРМОСТАТ", L"ИНТЕГРАТОР",
            L"КУЛОНОВСКИЙ РЕШАТЕЛЬ",
            L"ГРАНИЦЫ", L"ГРАВИТАЦИЯ",
            L"ГРАФИКИ И ЗАПИСЬ"};
        const wchar_t* values[]{
            interaction_tool == InteractionTool::select
                ? L"ВЫБОР"
                : interaction_tool == InteractionTool::spring_drag
                ? L"ПРУЖИНА" : L"СИЛА",
            simulation_engine.config().thermostat == simulation::Thermostat::none
                ? L"ВЫКЛ" : L"ВКЛ",
            simulation_engine.config().integrator ==
                    simulation::Integrator::velocity_verlet
                ? L"VELOCITY VERLET" : L"KICK-DRIFT-KICK",
            simulation_engine.config().coulomb_solver ==
                    simulation::CoulombSolver::barnes_hut
                ? L"BARNES-HUT" : L"ПРЯМОЙ",
            simulation_engine.config().boundary_mode ==
                    simulation::BoundaryMode::none
                ? L"ВЫКЛ" : L"ВКЛ",
            simulation_engine.config().gravity_z == 0.0
                ? L"ВЫКЛ" : L"ВКЛ",
            L"ОТКРЫТЬ"};
        for (int index = 0; index < 7; ++index)
        {
            const RECT button = tools_panel_button_area(client_area, index);
            draw_tool_button(button, labels[index], false);
            draw_text(device_context, values[index],
                RECT{button.left + 8, button.top + 13,
                    button.right - 8, button.bottom},
                x_axis_color, small_font, DT_RIGHT);
        }
        wchar_t speed_label[32]{};
        std::swprintf(speed_label, std::size(speed_label),
            L"Скорость времени: %.2fx", time_flow_factor);
        draw_text(device_context, speed_label,
            RECT{panel.left + 14, panel.top + 342,
                panel.right - 14, panel.top + 368},
            muted_text_color, small_font);
        return;
    }
    if (workspace_page == WorkspacePage::settings)
    {
        for (int index = 0; index < 5; ++index)
        {
            draw_tool_button(tools_panel_button_area(client_area, index),
                style_names[index],
                static_cast<int>(visual_style) == index);
        }
        const wchar_t* settings_labels[]{
            L"КАЧЕСТВО ГРАФИКИ", L"СЕТКА",
            L"ОКНО ГРАФИКОВ", L"ДОП. НАСТРОЙКИ",
            L"АВТОНАСТРОЙКА ПК", L"ПЕРЕЗАПУСК"};
        for (int index = 0; index < 6; ++index)
        {
            draw_tool_button(tools_panel_button_area(client_area, index + 5),
                settings_labels[index], false);
        }
        return;
    }
    if (workspace_page == WorkspacePage::experiments)
    {
        const wchar_t* labels[]{
            L"ТАБЛИЦА ЭЛЕМЕНТОВ",
            L"НАЗНАЧИТЬ ЭЛЕМЕНТ ГРУППЕ",
            L"ПАРАМЕТРЫ ГРУППЫ",
            L"ТЕПЛОВОЙ ИМПУЛЬС",
            L"СБРОСИТЬ СКОРОСТИ",
            L"СМЕНИТЬ ЗНАК ЗАРЯДА",
            L"ОБРАЗЕЦ ГАЗА · 64",
            L"ИОННАЯ ПАРА"};
        const elements::Element* element =
            elements::find(selected_element_number);
        for (int index = 0; index < 8; ++index)
        {
            const RECT button = tools_panel_button_area(client_area, index);
            draw_tool_button(button, labels[index], false);
        }
        if (element != nullptr)
        {
            wchar_t element_label[96]{};
            std::swprintf(element_label, std::size(element_label),
                L"АКТИВНЫЙ ЭЛЕМЕНТ: %s · Z=%d",
                element->symbol, element->atomic_number);
            draw_text(device_context, element_label,
                RECT{panel.left + 12, panel.top + 352,
                    panel.right - 12, panel.top + 374},
                element_color(element->atomic_number), small_font);
            wchar_t model_label[96]{};
            std::swprintf(model_label, std::size(model_label),
                L"Масса %.3g а.е.м. · радиус %.0f пм",
                element->atomic_mass, element->covalent_radius_pm);
            draw_text(device_context, model_label,
                RECT{panel.left + 12, panel.top + 376,
                    panel.right - 12, panel.top + 398},
                muted_text_color, small_font);
        }
        return;
    }
    bool has_tool_selection = false;
    bool selection_is_frozen = true;
    const auto& particles = simulation_engine.particles();
    if (selected_particles.empty())
    {
        if (selected_molecule_id > 0 &&
            selected_molecule_id <= particles.size())
        {
            has_tool_selection = true;
            selection_is_frozen =
                particles[static_cast<std::size_t>(selected_molecule_id - 1)]
                    .fixed;
        }
    }
    else
    {
        for (const std::size_t index : selected_particles)
        {
            if (index < particles.size())
            {
                has_tool_selection = true;
                selection_is_frozen =
                    selection_is_frozen && particles[index].fixed;
            }
        }
    }
    const elements::Element* create_element =
        elements::find(selected_element_number);
    wchar_t create_molecule_label[64]{};
    std::swprintf(create_molecule_label, std::size(create_molecule_label),
        L"СОЗДАТЬ АТОМ: %s",
        create_element == nullptr ? L"?" : create_element->symbol);
    draw_tool_button(tools_panel_button_area(client_area, 0),
        molecule_tool_active ? L"ОТМЕНИТЬ РАЗМЕЩЕНИЕ"
                             : create_molecule_label,
        molecule_tool_active);
    draw_tool_button(tools_panel_button_area(client_area, 1),
        bond_tool_active ? L"СОЗДАНИЕ СВЯЗИ: ВКЛ" : L"СОЗДАТЬ СВЯЗЬ",
        bond_tool_active);
    draw_tool_button(tools_panel_button_area(client_area, 2),
        L"ВЫБОР / ПРУЖИНА / СИЛА", molecule_dragging);
    draw_tool_button(tools_panel_button_area(client_area, 3),
        has_tool_selection && selection_is_frozen
            ? L"РАЗМОРОЗИТЬ ВЫБРАННЫЕ"
            : L"ЗАМОРОЗИТЬ ВЫБРАННЫЕ",
        has_tool_selection && selection_is_frozen);
    draw_tool_button(tools_panel_button_area(client_area, 4),
        molecular_tool == MolecularTool::multi_select
            ? L"ВЫБОР ГРУППЫ: ВКЛ" : L"ВЫБОР НЕСКОЛЬКИХ",
        molecular_tool == MolecularTool::multi_select);
    wchar_t lattice_label[48]{};
    std::swprintf(lattice_label,
        sizeof(lattice_label) / sizeof(lattice_label[0]),
        L"КВАДРАТНАЯ РЕШЁТКА (%llu)",
        static_cast<unsigned long long>(square_lattice_count));
    draw_tool_button(tools_panel_button_area(client_area, 5),
        lattice_label, molecular_tool == MolecularTool::square_lattice);
    draw_tool_button(tools_panel_button_area(client_area, 6),
        L"СЦЕНАРИЙ: ДОБАВИТЬ ШАГ", false);
    draw_tool_button(tools_panel_button_area(client_area, 7),
        scenario_running ? L"ОСТАНОВИТЬ СЦЕНАРИЙ" :
            L"ЗАПУСТИТЬ СЦЕНАРИЙ",
        scenario_running);
    draw_tool_button(tools_panel_button_area(client_area, 8),
        L"ОЧИСТИТЬ СЦЕНАРИЙ", false);
    draw_tool_button(tools_panel_button_area(client_area, 9),
        wall_tool_active
            ? L"СТЕНА: ПРОТЯНИТЕ"
            : simulation_engine.config().two_dimensional
            ? L"СТЕНА: ЛИНИЯ 2D" : L"СТЕНА: ПЛОСКОСТЬ 3D",
        wall_tool_active);
    draw_tool_button(tools_panel_button_area(client_area, 10),
        simulation_engine.config().two_dimensional
            ? L"РАЗМЕРНОСТЬ: 2D → 3D"
            : L"РАЗМЕРНОСТЬ: 3D → 2D",
        simulation_engine.config().two_dimensional);
    const elements::Element* active_element =
        elements::find(selected_element_number);
    if (active_element != nullptr)
    {
        wchar_t element_button[64]{};
        std::swprintf(element_button, std::size(element_button),
            L"ТАБЛИЦА ЭЛЕМЕНТОВ · %s", active_element->symbol);
        draw_tool_button(tools_panel_button_area(client_area, 11),
            element_button, false);
    }

    wchar_t selection_label[48]{};
    std::swprintf(selection_label,
        sizeof(selection_label) / sizeof(selection_label[0]),
        L"ВЫБРАНО: %llu",
        static_cast<unsigned long long>(selected_particles.empty() &&
                selected_molecule_id != 0 ? 1 : selected_particles.size()));
    draw_text(device_context, selection_label,
        RECT{panel.left + 12, panel.top + 452,
            panel.right - 12, panel.top + 316},
        muted_text_color, small_font);
    if (!scenario_steps.empty())
    {
        wchar_t scenario_label[48]{};
        std::swprintf(scenario_label,
            sizeof(scenario_label) / sizeof(scenario_label[0]),
            L"ШАГОВ СЦЕНАРИЯ: %llu",
            static_cast<unsigned long long>(scenario_steps.size()));
        draw_text(device_context, scenario_label,
            RECT{panel.left + 12, panel.top + 474,
                panel.right - 12, panel.top + 338},
            muted_text_color, small_font);
    }
}

void draw_overlay(HDC device_context, const RECT& client_area)
{
    if (separate_interface_windows)
    {
        return;
    }
    if (toolbar_visible)
    {
        draw_toolbar_overlay(device_context, client_area);
    }
    if (small_font != nullptr)
    {
        draw_workspace_rail(device_context, client_area);
    }
    if (footer_visible)
    {
        draw_footer_overlay(device_context, client_area);
    }
    draw_tools_panel_overlay(device_context, client_area);
}

bool set_window_long_checked(HWND window, int index, LONG_PTR value);
void set_separate_interface_windows(HWND window, bool enabled);
void show_detached_settings_window(HWND owner);
void show_chart_window(HWND owner);
void show_periodic_table(HWND owner);
void show_graphics_menu(HWND window, bool force_popup = false);

void toggle_fullscreen(HWND window)
{
    if (separate_interface_windows)
    {
        MessageBoxW(window,
            L"Полноэкранный режим отключён, пока панели и инструменты "
            L"находятся в отдельном окне. Сначала объедините окна.",
            L"Bonderium", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!fullscreen)
    {
        windowed_placement.length = sizeof(windowed_placement);
        if (!GetWindowPlacement(window, &windowed_placement))
        {
            MessageBoxW(window, L"Unable to save the current window placement.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        windowed_style = GetWindowLongPtrW(window, GWL_STYLE);
        windowed_extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
        const HMONITOR monitor =
            MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info))
        {
            MessageBoxW(window, L"Unable to determine the display bounds.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        const LONG_PTR fullscreen_style =
            (windowed_style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
            WS_POPUP;
        const LONG_PTR fullscreen_extended_style =
            windowed_extended_style & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE);
        if (!set_window_long_checked(window, GWL_STYLE, fullscreen_style) ||
            !set_window_long_checked(
                window, GWL_EXSTYLE, fullscreen_extended_style))
        {
            set_window_long_checked(window, GWL_STYLE, windowed_style);
            set_window_long_checked(
                window, GWL_EXSTYLE, windowed_extended_style);
            MessageBoxW(window, L"Unable to change the window display mode.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        if (!SetWindowPos(window, HWND_TOP,
                monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER))
        {
            set_window_long_checked(window, GWL_STYLE, windowed_style);
            set_window_long_checked(
                window, GWL_EXSTYLE, windowed_extended_style);
            SetWindowPlacement(window, &windowed_placement);
            MessageBoxW(window, L"Unable to enter fullscreen mode.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        fullscreen = true;
    }
    else
    {
        if (!set_window_long_checked(window, GWL_STYLE, windowed_style) ||
            !set_window_long_checked(
                window, GWL_EXSTYLE, windowed_extended_style) ||
            !SetWindowPlacement(window, &windowed_placement) ||
            !SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                    SWP_NOZORDER | SWP_NOOWNERZORDER))
        {
            MessageBoxW(window, L"Unable to restore the windowed display.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        fullscreen = false;
    }

    InvalidateRect(window, nullptr, FALSE);
}

void configure_graphics_for_this_pc(HWND window)
{
    const int choice = MessageBoxW(window,
        L"Bonderium will check the processor, installed memory and display "
        L"resolution, then choose a graphics quality preset for this PC. "
        L"This only changes local rendering settings and does not send "
        L"hardware information anywhere. Continue?",
        L"Automatic PC configuration",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (choice != IDYES)
    {
        return;
    }

    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status))
    {
        MessageBoxW(window, L"Unable to read the installed memory amount.",
            L"Automatic PC configuration", MB_OK | MB_ICONERROR);
        return;
    }

    const HMONITOR monitor =
        MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info))
    {
        MessageBoxW(window, L"Unable to read the display resolution.",
            L"Automatic PC configuration", MB_OK | MB_ICONERROR);
        return;
    }

    const unsigned long long memory_gigabytes =
        memory_status.ullTotalPhys / (1024ULL * 1024ULL * 1024ULL);
    const int display_width =
        monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
    const int display_height =
        monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
    if (system_info.dwNumberOfProcessors <= 2 || memory_gigabytes < 6 ||
        display_width < 1280 || display_height < 720)
    {
        graphics_quality = GraphicsQuality::performance;
    }
    else if (system_info.dwNumberOfProcessors >= 8 &&
        memory_gigabytes >= 16 && display_width >= 1920 &&
        display_height >= 1080)
    {
        graphics_quality = GraphicsQuality::quality;
    }
    else
    {
        graphics_quality = GraphicsQuality::balanced;
    }

    InvalidateRect(window, nullptr, FALSE);
}

bool update_process_memory_usage()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)))
    {
        process_memory_available = false;
        return false;
    }

    process_private_bytes =
        static_cast<std::uint64_t>(counters.PrivateUsage);
    process_memory_available = true;
    return true;
}

bool ram_budget_allows_molecule(HWND window, std::size_t additional_count = 1)
{
    if (ram_budget_bytes == 0)
    {
        return true;
    }
    if (!update_process_memory_usage())
    {
        MessageBoxW(window,
            L"Unable to measure memory usage. The requested particles were "
            L"not added "
            L"because a RAM budget is active.",
            L"Memory budget", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::size_t particle_count =
        simulation_engine.particles().size();
    const std::size_t current_pairs =
        simulation_engine.metrics().neighbor_pairs;
    const std::uint64_t estimated_neighbors =
        particle_count == 0 ? 16 :
        (std::max)(std::uint64_t{16},
            static_cast<std::uint64_t>(current_pairs) * 2 /
                particle_count);
    constexpr std::uint64_t estimated_particle_bytes = 1024;
    constexpr std::uint64_t estimated_neighbor_pair_bytes = 16;
    const std::uint64_t estimated_bytes_per_molecule =
        estimated_particle_bytes +
        estimated_neighbors * estimated_neighbor_pair_bytes;
    const std::uint64_t projected_bytes = process_private_bytes +
        estimated_bytes_per_molecule * additional_count;
    if (projected_bytes <= ram_budget_bytes)
    {
        return true;
    }

    MessageBoxW(window,
        L"Adding the requested molecule(s) would exceed the configured RAM "
        L"budget. Increase the budget in OPT or remove molecules.",
        L"Memory budget reached", MB_OK | MB_ICONWARNING);
    return false;
}

struct RamBudgetDialogState
{
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    std::uint64_t budget_bytes = 0;
};

LRESULT CALLBACK ram_budget_dialog_procedure(
    HWND dialog, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* state = reinterpret_cast<RamBudgetDialogState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* creation =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<RamBudgetDialogState*>(
            creation->lpCreateParams);
        SetWindowLongPtrW(dialog, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr)
    {
        return DefWindowProcW(dialog, message, w_param, l_param);
    }

    if (message == WM_CREATE)
    {
        CreateWindowExW(0, L"STATIC",
            L"Set a RAM budget in megabytes (128-65536):",
            WS_CHILD | WS_VISIBLE, 18, 18, 320, 22, dialog, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            18, 46, 300, 26, dialog,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(101)),
            GetModuleHandleW(nullptr), nullptr);
        if (state->edit == nullptr ||
            CreateWindowExW(0, L"BUTTON", L"Apply",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                154, 86, 78, 28, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                GetModuleHandleW(nullptr), nullptr) == nullptr ||
            CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                240, 86, 78, 28, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                GetModuleHandleW(nullptr), nullptr) == nullptr)
        {
            return -1;
        }
        const std::uint64_t current_megabytes =
            ram_budget_bytes / (1024ULL * 1024ULL);
        wchar_t initial_value[24]{};
        std::swprintf(initial_value,
            sizeof(initial_value) / sizeof(initial_value[0]),
            L"%llu", static_cast<unsigned long long>(
                current_megabytes == 0 ? 1024 : current_megabytes));
        SetWindowTextW(state->edit, initial_value);
        SetFocus(state->edit);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(w_param) == IDOK)
    {
        wchar_t text[32]{};
        GetWindowTextW(state->edit, text,
            static_cast<int>(std::size(text)));
        wchar_t* end = nullptr;
        const unsigned long long megabytes = std::wcstoull(text, &end, 10);
        if (end == text || *end != L'\0' ||
            megabytes < 128 || megabytes > 65536)
        {
            MessageBoxW(dialog,
                L"Enter a whole number from 128 to 65536 MB.",
                L"Invalid RAM budget", MB_OK | MB_ICONWARNING);
            SetFocus(state->edit);
            return 0;
        }
        state->budget_bytes = megabytes * 1024ULL * 1024ULL;
        state->accepted = true;
        state->done = true;
        DestroyWindow(dialog);
        return 0;
    }
    if ((message == WM_COMMAND && LOWORD(w_param) == IDCANCEL) ||
        message == WM_CLOSE)
    {
        state->done = true;
        DestroyWindow(dialog);
        return 0;
    }
    return DefWindowProcW(dialog, message, w_param, l_param);
}

void set_custom_ram_budget(HWND owner)
{
    static constexpr wchar_t dialog_class_name[] =
        L"BonderiumRamBudgetDialog";
    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = ram_budget_dialog_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        window_class.lpszClassName = dialog_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(owner, L"Unable to open the RAM budget editor.",
                L"Memory budget", MB_OK | MB_ICONERROR);
            return;
        }
        class_registered = true;
    }

    RamBudgetDialogState state;
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int left = owner_rect.left +
        ((owner_rect.right - owner_rect.left) - 350) / 2;
    const int top = owner_rect.top +
        ((owner_rect.bottom - owner_rect.top) - 155) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME,
        dialog_class_name, L"RAM budget",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        left, top, 350, 155, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr)
    {
        MessageBoxW(owner, L"Unable to create the RAM budget editor.",
            L"Memory budget", MB_OK | MB_ICONERROR);
        return;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (!state.done)
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0)
        {
            PostQuitMessage(static_cast<int>(message.wParam));
            state.done = true;
            break;
        }
        if (result == -1)
        {
            state.done = true;
            DestroyWindow(dialog);
            EnableWindow(owner, TRUE);
            MessageBoxW(owner, L"The RAM budget editor could not continue.",
                L"Memory budget", MB_OK | MB_ICONERROR);
            return;
        }
        if (!IsDialogMessageW(dialog, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.accepted)
    {
        ram_budget_bytes = state.budget_bytes;
        update_process_memory_usage();
        InvalidateRect(owner, nullptr, FALSE);
    }
}

void restart_application(HWND window)
{
    const int choice = MessageBoxW(window,
        L"Bonderium will close and start again. Molecules and simulation "
        L"state that have not been saved will be lost. Restart now?",
        L"Restart Bonderium", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES)
    {
        return;
    }

    wchar_t executable_path[MAX_PATH]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD path_length = GetModuleFileNameW(
        nullptr, executable_path, static_cast<DWORD>(std::size(executable_path)));
    if (path_length == 0 ||
        path_length >= static_cast<DWORD>(std::size(executable_path)))
    {
        MessageBoxW(window, L"Unable to locate the Bonderium executable.",
            L"Restart Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    const HINSTANCE launch_result = ShellExecuteW(window, L"open",
        executable_path, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(launch_result) <= 32)
    {
        MessageBoxW(window, L"Unable to start a replacement Bonderium process.",
            L"Restart Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    PostMessageW(window, WM_CLOSE, 0, 0);
}

bool set_window_long_checked(HWND window, int index, LONG_PTR value)
{
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(window, index, value);
    return GetLastError() == ERROR_SUCCESS;
}

void show_graphics_menu(HWND window, bool force_popup)
{
    if (separate_interface_windows && !force_popup)
    {
        show_detached_settings_window(window);
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        MessageBoxW(window, L"Unable to open graphics settings.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    UINT fullscreen_flags = MF_STRING;
    if (separate_interface_windows)
    {
        fullscreen_flags |= MF_DISABLED | MF_GRAYED;
    }
    if (fullscreen)
    {
        fullscreen_flags |= MF_CHECKED;
    }
    UINT grid_flags = MF_STRING;
    if (grid_visible)
    {
        grid_flags |= MF_CHECKED;
    }
    const UINT quality_performance_flags =
        MF_STRING | (graphics_quality == GraphicsQuality::performance
                ? MF_CHECKED : 0);
    const UINT quality_balanced_flags =
        MF_STRING | (graphics_quality == GraphicsQuality::balanced
                ? MF_CHECKED : 0);
    const UINT quality_high_flags =
        MF_STRING | (graphics_quality == GraphicsQuality::quality
                ? MF_CHECKED : 0);
    const auto ram_option_flags = [](std::uint64_t bytes) {
        return MF_STRING |
            (ram_budget_bytes == bytes ? MF_CHECKED : 0);
    };
    const UINT custom_ram_flags =
        MF_STRING |
        (ram_budget_bytes != 0 &&
                ram_budget_bytes != 256ULL * 1024 * 1024 &&
                ram_budget_bytes != 512ULL * 1024 * 1024 &&
                ram_budget_bytes != 1024ULL * 1024 * 1024 &&
                ram_budget_bytes != 2ULL * 1024 * 1024 * 1024 &&
                ram_budget_bytes != 4ULL * 1024 * 1024 * 1024 &&
                ram_budget_bytes != 8ULL * 1024 * 1024 * 1024
            ? MF_CHECKED : 0);

    const bool menu_ready =
        AppendMenuW(menu, fullscreen_flags, menu_toggle_fullscreen,
            L"Fullscreen  (F11)") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(menu, MF_STRING |
                (separate_interface_windows ? MF_CHECKED : 0),
            menu_separate_interface,
            L"Панели и инструменты в отдельном окне") &&
        AppendMenuW(menu, MF_STRING |
                (separate_interface_windows ? 0 : MF_GRAYED),
            menu_open_detached_settings, L"Открыть отдельное окно настроек") &&
        AppendMenuW(menu, MF_STRING, menu_open_charts,
            L"Окно графиков и записи данных") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"Rendering quality") &&
        AppendMenuW(menu, quality_performance_flags,
            menu_quality_performance, L"Performance") &&
        AppendMenuW(menu, quality_balanced_flags,
            menu_quality_balanced, L"Balanced") &&
        AppendMenuW(menu, quality_high_flags,
            menu_quality_high, L"High quality") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(menu, grid_flags, menu_toggle_grid, L"Show grid") &&
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"RAM budget") &&
        AppendMenuW(menu, ram_option_flags(0),
            menu_ram_unlimited, L"Unlimited") &&
        AppendMenuW(menu, ram_option_flags(256ULL * 1024 * 1024),
            menu_ram_256_mb, L"256 MB") &&
        AppendMenuW(menu, ram_option_flags(512ULL * 1024 * 1024),
            menu_ram_512_mb, L"512 MB") &&
        AppendMenuW(menu, ram_option_flags(1024ULL * 1024 * 1024),
            menu_ram_1_gb, L"1 GB") &&
        AppendMenuW(menu, ram_option_flags(2ULL * 1024 * 1024 * 1024),
            menu_ram_2_gb, L"2 GB") &&
        AppendMenuW(menu, ram_option_flags(4ULL * 1024 * 1024 * 1024),
            menu_ram_4_gb, L"4 GB") &&
        AppendMenuW(menu, ram_option_flags(8ULL * 1024 * 1024 * 1024),
            menu_ram_8_gb, L"8 GB") &&
        AppendMenuW(menu, custom_ram_flags, menu_ram_custom, L"Custom...");
    if (!menu_ready)
    {
        DestroyMenu(menu);
        MessageBoxW(window, L"Unable to build the graphics settings menu.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT button = graphics_tool_area(client_area);
    POINT menu_position{button.left, button.bottom};
    ClientToScreen(window, &menu_position);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        menu_position.x, menu_position.y, 0, window, nullptr);
    DestroyMenu(menu);

    switch (command)
    {
    case menu_toggle_fullscreen:
        toggle_fullscreen(window);
        break;
    case menu_separate_interface:
        set_separate_interface_windows(window, !separate_interface_windows);
        break;
    case menu_open_detached_settings:
        show_detached_settings_window(window);
        break;
    case menu_open_charts:
        show_chart_window(window);
        break;
    case menu_quality_performance:
        graphics_quality = GraphicsQuality::performance;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_quality_balanced:
        graphics_quality = GraphicsQuality::balanced;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_quality_high:
        graphics_quality = GraphicsQuality::quality;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_toggle_grid:
        grid_visible = !grid_visible;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_unlimited:
        ram_budget_bytes = 0;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_256_mb:
        ram_budget_bytes = 256ULL * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_512_mb:
        ram_budget_bytes = 512ULL * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_1_gb:
        ram_budget_bytes = 1024ULL * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_2_gb:
        ram_budget_bytes = 2ULL * 1024 * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_4_gb:
        ram_budget_bytes = 4ULL * 1024 * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_8_gb:
        ram_budget_bytes = 8ULL * 1024 * 1024 * 1024;
        InvalidateRect(window, nullptr, FALSE);
        break;
    case menu_ram_custom:
        set_custom_ram_budget(window);
        break;
    default:
        break;
    }
}

void show_panels_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        MessageBoxW(window, L"Unable to open panel visibility settings.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    const bool menu_ready =
        AppendMenuW(menu, MF_STRING |
                (toolbar_visible ? MF_CHECKED : 0),
            menu_panel_toolbar, L"Top toolbar") &&
        AppendMenuW(menu, MF_STRING |
                (footer_visible ? MF_CHECKED : 0),
            menu_panel_footer, L"Bottom status panel") &&
        AppendMenuW(menu, MF_STRING |
                (tools_panel_visible ? MF_CHECKED : 0),
            menu_panel_tools, L"Tools panel");
    if (!menu_ready)
    {
        DestroyMenu(menu);
        MessageBoxW(window, L"Unable to build panel visibility settings.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    POINT menu_position{};
    GetCursorPos(&menu_position);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        menu_position.x, menu_position.y, 0, window, nullptr);
    DestroyMenu(menu);
    switch (command)
    {
    case menu_panel_toolbar:
        toolbar_visible = !toolbar_visible;
        break;
    case menu_panel_footer:
        footer_visible = !footer_visible;
        break;
    case menu_panel_tools:
        tools_panel_visible = !tools_panel_visible;
        break;
    default:
        return;
    }
    InvalidateRect(window, nullptr, FALSE);
}

void resize_detached_interface_window(HWND window)
{
    if (detached_interface_window == nullptr)
    {
        return;
    }

    RECT client_area{};
    GetClientRect(window, &client_area);
    RECT outer_area{0, 0, client_area.right, 500};
    const DWORD style = static_cast<DWORD>(
        GetWindowLongPtrW(detached_interface_window, GWL_STYLE));
    const DWORD extended_style = static_cast<DWORD>(
        GetWindowLongPtrW(detached_interface_window, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&outer_area, style, FALSE, extended_style))
    {
        MessageBoxW(window,
            L"Не удалось изменить размер отдельного окна панелей.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    const int width = outer_area.right - outer_area.left;
    const int height = outer_area.bottom - outer_area.top;
    RECT main_rect{};
    if (!GetWindowRect(window, &main_rect))
    {
        MessageBoxW(window,
            L"Не удалось определить положение главного окна.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    const HMONITOR monitor =
        MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (!GetMonitorInfoW(monitor, &monitor_info))
    {
        MessageBoxW(window,
            L"Не удалось определить рабочую область экрана.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    const int work_left = static_cast<int>(monitor_info.rcWork.left);
    const int work_top = static_cast<int>(monitor_info.rcWork.top);
    const int work_right = static_cast<int>(monitor_info.rcWork.right);
    const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
    int left = main_rect.right + 8;
    if (left + width > work_right)
    {
        const int left_of_main = main_rect.left - width - 8;
        left = left_of_main >= work_left
            ? left_of_main
            : (std::max)(work_left, work_right - width);
    }
    left = (std::clamp)(left, work_left,
        (std::max)(work_left, work_right - width));
    const int top = (std::clamp)(static_cast<int>(main_rect.top), work_top,
        (std::max)(work_top, work_bottom - height));
    if (!SetWindowPos(detached_interface_window, HWND_TOP, left, top,
            width, height, SWP_NOACTIVATE))
    {
        MessageBoxW(window,
            L"Не удалось синхронизировать размер окна панелей.",
            L"Bonderium", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK detached_interface_procedure(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }
    if (message == WM_CREATE)
    {
        SetTimer(window, 1, 300, nullptr);
        return 0;
    }
    if (message == WM_TIMER)
    {
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC context = BeginPaint(window, &paint);
        RECT client_area{};
        GetClientRect(window, &client_area);
        fill_rectangle(context, client_area, canvas_color);
        if (small_font != nullptr)
        {
            draw_workspace_rail(context, client_area);
        }
        if (toolbar_visible)
        {
            draw_toolbar_overlay(context, client_area);
        }
        if (tools_panel_visible)
        {
            draw_tools_panel_overlay(context, client_area);
        }
        if (footer_visible)
        {
            draw_footer_overlay(context, client_area);
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN)
    {
        if (main_window == nullptr)
        {
            return 0;
        }
        const POINT position{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT toolbar = toolbar_area(client_area);
        const RECT rail{client_area.left,
            client_area.top + toolbar_height,
            client_area.left + workspace_rail_width,
            client_area.bottom};
        const RECT tools = tools_panel_area(client_area);
        const bool on_rail = PtInRect(&rail, position);
        if (on_rail)
        {
            const WorkspacePage pages[]{
                WorkspacePage::objects,
                WorkspacePage::thermodynamics,
                WorkspacePage::dynamics,
                WorkspacePage::quantum,
                WorkspacePage::settings,
                WorkspacePage::experiments};
            for (const WorkspacePage page : pages)
            {
                const RECT category = workspace_category_area(client_area, page);
                if (PtInRect(&category, position))
                {
                    workspace_page = workspace_page == page
                        ? WorkspacePage::closed : page;
                    tools_panel_visible =
                        workspace_page != WorkspacePage::closed;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }
        }
        const bool on_toolbar = toolbar_visible &&
            PtInRect(&toolbar, position);
        const bool on_tools = tools_panel_visible &&
            PtInRect(&tools, position);
        const RECT footer = footer_area(client_area);
        const bool on_footer = footer_visible &&
            PtInRect(&footer, position);
        if (!on_toolbar && !on_rail && !on_tools && !on_footer)
        {
            return 0;
        }
        if (on_footer)
        {
            const RECT close_button{
                footer.right - 34, footer.top,
                footer.right - 4, footer.top + 24};
            if (PtInRect(&close_button, position))
            {
                footer_visible = false;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        }
        SendMessageW(main_window, WM_LBUTTONDOWN, w_param, l_param);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KEYDOWN && main_window != nullptr)
    {
        SendMessageW(main_window, message, w_param, l_param);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_CLOSE)
    {
        set_separate_interface_windows(main_window, false);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(window, 1);
        if (detached_interface_window == window)
        {
            detached_interface_window = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK detached_settings_procedure(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        RECT client_area{};
        GetClientRect(window, &client_area);
        if (detached_settings_background != nullptr)
        {
            FillRect(reinterpret_cast<HDC>(w_param), &client_area,
                detached_settings_background);
        }
        return 1;
    }
    if (message == WM_CREATE)
    {
        detached_settings_background = CreateSolidBrush(toolbar_color);
        if (detached_settings_background == nullptr)
        {
            return -1;
        }
        if (CreateWindowExW(0, L"STATIC",
                L"Сцена остаётся в отдельном оконном режиме.",
            WS_CHILD | WS_VISIBLE, 18, 18, 500, 24, window, nullptr,
                GetModuleHandleW(nullptr), nullptr) == nullptr ||
            CreateWindowExW(0, L"STATIC",
                L"Полный экран недоступен, пока окна не объединены.",
            WS_CHILD | WS_VISIBLE, 18, 46, 500, 24, window, nullptr,
                GetModuleHandleW(nullptr), nullptr) == nullptr ||
            CreateWindowExW(0, L"BUTTON", L"Настройки графики...",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            18, 92, 155, 32, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(102)),
                GetModuleHandleW(nullptr), nullptr) == nullptr ||
        CreateWindowExW(0, L"BUTTON", L"Окно графиков",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            182, 92, 145, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(103)),
            GetModuleHandleW(nullptr), nullptr) == nullptr ||
        CreateWindowExW(0, L"BUTTON", L"Объединить окна",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            336, 92, 170, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(101)),
                GetModuleHandleW(nullptr), nullptr) == nullptr)
        {
            return -1;
        }
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC)
    {
        HDC context = reinterpret_cast<HDC>(w_param);
        SetTextColor(context, primary_text_color);
        SetBkColor(context, toolbar_color);
        return reinterpret_cast<LRESULT>(detached_settings_background);
    }
    if (message == WM_DRAWITEM)
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
        fill_rectangle(item->hDC, item->rcItem, surface_color);
        draw_line(item->hDC, item->rcItem.left, item->rcItem.top,
            item->rcItem.right, item->rcItem.top, border_color);
        draw_line(item->hDC, item->rcItem.left, item->rcItem.bottom - 1,
            item->rcItem.right, item->rcItem.bottom - 1, border_color);
        draw_line(item->hDC, item->rcItem.left, item->rcItem.top,
            item->rcItem.left, item->rcItem.bottom, border_color);
        draw_line(item->hDC, item->rcItem.right - 1, item->rcItem.top,
            item->rcItem.right - 1, item->rcItem.bottom, border_color);
        wchar_t label[80]{};
        GetWindowTextW(item->hwndItem, label,
            static_cast<int>(std::size(label)));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, primary_text_color);
        HFONT previous_font = nullptr;
        if (button_font != nullptr)
        {
            previous_font = static_cast<HFONT>(
                SelectObject(item->hDC, button_font));
        }
        RECT text_area = item->rcItem;
        DrawTextW(item->hDC, label, -1, &text_area,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (previous_font != nullptr)
        {
            SelectObject(item->hDC, previous_font);
        }
        return TRUE;
    }
    if (message == WM_COMMAND)
    {
        switch (LOWORD(w_param))
        {
        case 101:
            set_separate_interface_windows(main_window, false);
            return 0;
        case 102:
            if (main_window != nullptr)
            {
                show_graphics_menu(main_window, true);
            }
            return 0;
        case 103:
            if (main_window != nullptr)
            {
                show_chart_window(main_window);
            }
            return 0;
        default:
            break;
        }
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && detached_settings_window == window)
    {
        detached_settings_window = nullptr;
        if (detached_settings_background != nullptr)
        {
            DeleteObject(detached_settings_background);
            detached_settings_background = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void set_separate_interface_windows(HWND window, bool enabled)
{
    if (enabled == separate_interface_windows)
    {
        return;
    }
    if (!enabled)
    {
        separate_interface_windows = false;
        if (detached_settings_window != nullptr)
        {
            DestroyWindow(detached_settings_window);
            detached_settings_window = nullptr;
        }
        if (detached_interface_window != nullptr)
        {
            DestroyWindow(detached_interface_window);
            detached_interface_window = nullptr;
        }
        if (window != nullptr)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
        return;
    }
    if (window == nullptr)
    {
        return;
    }
    if (fullscreen)
    {
        toggle_fullscreen(window);
        if (fullscreen)
        {
            return;
        }
    }

    static bool interface_class_registered = false;
    if (!interface_class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = detached_interface_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = detached_interface_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(window,
                L"Не удалось зарегистрировать окно панелей.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        interface_class_registered = true;
    }

    RECT main_rect{};
    RECT client_area{};
    GetWindowRect(window, &main_rect);
    GetClientRect(window, &client_area);
    RECT detached_rect{0, 0, client_area.right, 500};
    if (!AdjustWindowRectEx(&detached_rect, WS_OVERLAPPEDWINDOW,
            FALSE, 0))
    {
        MessageBoxW(window,
            L"Не удалось определить размер окна панелей.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    detached_interface_window = CreateWindowExW(0,
        detached_interface_class_name, L"Bonderium — панели и инструменты",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        main_rect.right + 8, main_rect.top,
        detached_rect.right - detached_rect.left,
        detached_rect.bottom - detached_rect.top,
        window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (detached_interface_window == nullptr)
    {
        MessageBoxW(window,
            L"Не удалось создать окно панелей.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    separate_interface_windows = true;
    ShowWindow(detached_interface_window, SW_SHOW);
    resize_detached_interface_window(window);
    SetForegroundWindow(detached_interface_window);
    BringWindowToTop(detached_interface_window);
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(detached_interface_window);
}

void show_detached_settings_window(HWND owner)
{
    if (detached_settings_window != nullptr)
    {
        ShowWindow(detached_settings_window, SW_SHOW);
        SetForegroundWindow(detached_settings_window);
        return;
    }

    static bool settings_class_registered = false;
    if (!settings_class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = detached_settings_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = detached_settings_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(owner,
                L"Unable to register the separate settings window.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return;
        }
        settings_class_registered = true;
    }

    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    detached_settings_window = CreateWindowExW(WS_EX_DLGMODALFRAME,
        detached_settings_class_name, L"Bonderium — настройки окна",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        owner_rect.left + 40, owner_rect.top + 40, 540, 180,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (detached_settings_window == nullptr)
    {
        MessageBoxW(owner,
            L"Unable to create the separate settings window.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    ShowWindow(detached_settings_window, SW_SHOW);
    UpdateWindow(detached_settings_window);
}

LRESULT CALLBACK input_dialog_procedure(
    HWND dialog, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* state = reinterpret_cast<InputDialogState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* creation =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<InputDialogState*>(creation->lpCreateParams);
        SetWindowLongPtrW(dialog, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr)
    {
        return DefWindowProcW(dialog, message, w_param, l_param);
    }
    if (message == WM_CREATE)
    {
        for (std::size_t index = 0; index < state->field_count; ++index)
        {
            const int top = 16 + static_cast<int>(index) * 54;
            CreateWindowExW(0, L"STATIC", state->labels[index],
                WS_CHILD | WS_VISIBLE, 18, top, 360, 20, dialog, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            state->edits[index] = CreateWindowExW(WS_EX_CLIENTEDGE,
                L"EDIT", state->values[index],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                18, top + 22, 360, 25, dialog,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(101 + index)),
                GetModuleHandleW(nullptr), nullptr);
            if (state->edits[index] == nullptr)
            {
                return -1;
            }
        }
        const int buttons_top =
            24 + static_cast<int>(state->field_count) * 54;
        if (CreateWindowExW(0, L"BUTTON", L"Применить",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                210, buttons_top, 80, 28, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                GetModuleHandleW(nullptr), nullptr) == nullptr ||
            CreateWindowExW(0, L"BUTTON", L"Отмена",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                298, buttons_top, 80, 28, dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                GetModuleHandleW(nullptr), nullptr) == nullptr)
        {
            return -1;
        }
        SetFocus(state->edits[0]);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(w_param) == IDOK)
    {
        for (std::size_t index = 0; index < state->field_count; ++index)
        {
            GetWindowTextW(state->edits[index], state->values[index],
                static_cast<int>(std::size(state->values[index])));
        }
        state->accepted = true;
        state->done = true;
        DestroyWindow(dialog);
        return 0;
    }
    if ((message == WM_COMMAND && LOWORD(w_param) == IDCANCEL) ||
        message == WM_CLOSE)
    {
        state->done = true;
        DestroyWindow(dialog);
        return 0;
    }
    return DefWindowProcW(dialog, message, w_param, l_param);
}

bool show_input_dialog(HWND owner, InputDialogState& state)
{
    static constexpr wchar_t dialog_class_name[] =
        L"BonderiumInputDialog";
    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = input_dialog_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        window_class.lpszClassName = dialog_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(owner, L"Не удалось открыть окно ввода.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return false;
        }
        class_registered = true;
    }

    const int height = 90 + static_cast<int>(state.field_count) * 54;
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int left = owner_rect.left +
        ((owner_rect.right - owner_rect.left) - 410) / 2;
    const int top = owner_rect.top +
        ((owner_rect.bottom - owner_rect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME,
        dialog_class_name, state.title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        left, top, 410, height, owner, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr)
    {
        MessageBoxW(owner, L"Не удалось создать окно ввода.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return false;
    }

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (!state.done)
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0)
        {
            PostQuitMessage(static_cast<int>(message.wParam));
            state.done = true;
            break;
        }
        if (result == -1)
        {
            DestroyWindow(dialog);
            EnableWindow(owner, TRUE);
            MessageBoxW(owner, L"Окно ввода было неожиданно закрыто.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            return false;
        }
        if (!IsDialogMessageW(dialog, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.accepted;
}

void show_help(HWND window)
{
    MessageBoxW(window,
        L"ГОРЯЧИЕ КЛАВИШИ\n"
        L"W A S D / стрелки  Перемещение камеры\n"
        L"Q / E  Камера вниз / вверх (только 3D)\n"
        L"Правая кнопка мыши + движение  Вращение камеры\n"
        L"Средняя кнопка мыши + движение  Панорамирование\n"
        L"Колесо мыши  Масштабирование\n"
        L"Ползунок TIME  Скорость симуляционного времени, 0,25×–4×\n"
        L"Home  Сброс камеры\n"
        L"F11  Полноэкранный режим\n"
        L"F2  Показать или скрыть панели\n"
        L"F1  Открыть эту справку\n"
        L"C  Разместить атом выбранного элемента\n"
        L"B  Создать связь между двумя атомами\n"
        L"Пробел  Запустить или приостановить симуляцию\n"
        L"[ / ]  Изменить заряд выбранного атома\n"
        L"Shift + [ / ]  Изменить целевую температуру\n"
        L"Esc  Отменить размещение атома или создание связи\n"
        L"2 / 3  Переключить размерность симуляции\n"
        L"\nИНСТРУМЕНТЫ И НАСТРОЙКИ\n"
        L"SELECT / SPRING / FORCE  Выбор режима взаимодействия\n"
        L"Левая панель  Объекты, термодинамика, динамика, лаборатория, настройки\n"
        L"Настройки → Оформление  Выбор одного из пяти стилей\n"
        L"Динамика  Параметры симуляции и окно графиков\n"
        L"Лаборатория  Таблица 118 элементов и инструменты опытов\n"
        L"\nИНСТРУМЕНТЫ И СЦЕНАРИИ\n"
        L"В разделе Объекты включите выбор нескольких и щёлкайте молекулы, "
        L"чтобы добавить их в группу или убрать из неё.\n"
        L"Таблица элементов: поиск по названию, символу или атомному номеру; "
        L"выбранный элемент используется при создании атомов.\n"
        L"В Лаборатории можно настроить параметры частиц, задать тепловые "
        L"скорости, инвертировать заряды и создать образцы газа или ионной пары.\n"
        L"Заморозка переключает выбранные молекулы между неподвижным и "
        L"подвижным состоянием.\n"
        L"Квадратная решётка запрашивает количество молекул; затем протяните "
        L"квадрат в сцене для равномерного размещения.\n"
        L"В сценарий можно добавить шаг силы для выбранных молекул или шаг "
        L"создания решётки по нарисованному квадрату.\n"
        L"Стена: протяните отрезок в 2D или прямоугольник в 3D; координаты "
        L"стены привязываются к целым значениям.\n"
        L"В 2D молекулы остаются в плоскости XY; переключатель размерности "
        L"находится в разделе Объекты.\n"
        L"Раздел Квантовая динамика пока является заготовкой для будущих "
        L"инструментов.\n"
        L"Сценарий проигрывается по времени симуляции и хранится до закрытия.\n"
        L"Крестик закрывает панель; клавиша F2 позволяет вернуть её.\n"
        L"Бюджет памяти предупреждает о достижении порога и приостанавливает "
        L"симуляцию, но не задаёт жёсткий лимит памяти Windows.",
        L"Bonderium — Справка", MB_OK | MB_ICONINFORMATION);
}

void set_interaction_tool(HWND window, InteractionTool tool)
{
    if (molecule_dragging)
    {
        const std::size_t active_index = interacting_particle;
        molecule_dragging = false;
        interacting_particle = static_cast<std::size_t>(-1);
        if (active_index < simulation_engine.particles().size())
        {
            simulation_engine.set_external_force(active_index, Vec3{});
        }
        ReleaseCapture();
    }
    interaction_tool = tool;
    molecule_tool_active = false;
    bond_tool_active = false;
    wall_tool_active = false;
    molecular_tool = MolecularTool::select;
    square_lattice_dragging = false;
    square_lattice_for_scenario = false;
    pending_bond_molecule_id = 0;
    invalidate_simulation_ui(window, true);
}

void show_interaction_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        MessageBoxW(window, L"Unable to open molecule interaction tools.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    const bool menu_ready =
        AppendMenuW(menu, MF_STRING |
                (interaction_tool == InteractionTool::select ? MF_CHECKED : 0),
            menu_interaction_select, L"Select molecule") &&
        AppendMenuW(menu, MF_STRING |
                (interaction_tool == InteractionTool::spring_drag
                        ? MF_CHECKED : 0),
            menu_interaction_spring_drag,
            L"Spring drag (requires running simulation)") &&
        AppendMenuW(menu, MF_STRING |
                (interaction_tool == InteractionTool::directional_force
                        ? MF_CHECKED : 0),
            menu_interaction_directional_force,
            L"Directional force (hold and drag)");
    if (!menu_ready)
    {
        DestroyMenu(menu);
        MessageBoxW(window, L"Unable to build the interaction tools menu.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT button = interaction_tool_area(client_area);
    POINT menu_position{button.left, button.bottom};
    ClientToScreen(window, &menu_position);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        menu_position.x, menu_position.y, 0, window, nullptr);
    DestroyMenu(menu);

    switch (command)
    {
    case menu_interaction_select:
        set_interaction_tool(window, InteractionTool::select);
        break;
    case menu_interaction_spring_drag:
        set_interaction_tool(window, InteractionTool::spring_drag);
        break;
    case menu_interaction_directional_force:
        set_interaction_tool(window, InteractionTool::directional_force);
        break;
    default:
        break;
    }
}

bool is_key_down(int key)
{
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

void start_camera_motion(HWND window)
{
    camera_state.last_motion_tick = GetTickCount64();
    SetTimer(window, camera_timer_id, motion_timer_interval_ms, nullptr);
}

void stop_camera_motion(HWND window)
{
    camera_state.velocity = Vec3{};
    camera_state.last_motion_tick = 0;
    KillTimer(window, camera_timer_id);
}

void set_molecule_tool(HWND window, bool active)
{
    molecule_tool_active = active;
    bond_tool_active = false;
    wall_tool_active = false;
    wall_dragging = false;
    molecular_tool = MolecularTool::select;
    square_lattice_dragging = false;
    square_lattice_for_scenario = false;
    pending_bond_molecule_id = 0;
    pointer_over_scene = false;
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT area = scene_area(client_area);
    const bool over_scene = active && PtInRect(&area, cursor);
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(over_scene ? 32515 : 32512)));
    if (toolbar_visible)
    {
        const RECT button_area = molecule_tool_area(client_area);
        InvalidateRect(window, &button_area, FALSE);
        const RECT simulation_button = simulation_tool_area(client_area);
        InvalidateRect(window, &simulation_button, FALSE);
    }
    if (footer_visible)
    {
        const RECT footer = footer_area(client_area);
        InvalidateRect(window, &footer, FALSE);
    }
    if (over_scene)
    {
        InvalidateRect(window, &area, FALSE);
    }
}

void set_bond_tool(HWND window, bool active)
{
    bond_tool_active = active;
    wall_tool_active = false;
    wall_dragging = false;
    pending_bond_molecule_id = 0;
    molecule_tool_active = false;
    molecular_tool = MolecularTool::select;
    square_lattice_dragging = false;
    square_lattice_for_scenario = false;
    pointer_over_scene = false;
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    invalidate_simulation_ui(window, true);
}

void stop_molecule_interaction(HWND window);
void stop_scenario(HWND window);

void set_simulation_running(HWND window, bool running)
{
    if (running && simulation_engine.particles().empty())
    {
        MessageBoxW(window,
            L"Place at least one molecule before starting the simulation.",
            L"Bonderium — Experimental molecular dynamics",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!running && molecule_dragging)
    {
        stop_molecule_interaction(window);
    }
    if (!running)
    {
        stop_scenario(window);
    }
    simulation_running = running;
    simulation_time_accumulator = 0.0;
    last_simulation_tick = GetTickCount64();
    if (simulation_running)
    {
        SetTimer(window, simulation_timer_id,
            simulation_timer_interval_ms, nullptr);
    }
    else
    {
        KillTimer(window, simulation_timer_id);
    }

    invalidate_simulation_ui(window, true);
}

Vec3 limit_interaction_force(Vec3 force)
{
    const double magnitude_squared = dot(force, force);
    const double maximum_squared =
        maximum_interaction_force * maximum_interaction_force;
    if (magnitude_squared > maximum_squared)
    {
        return force * (maximum_interaction_force /
            std::sqrt(magnitude_squared));
    }
    return force;
}

bool update_interaction_force(HWND window)
{
    if (!molecule_dragging ||
        interacting_particle >= simulation_engine.particles().size())
    {
        return true;
    }

    const auto& particle =
        simulation_engine.particles()[interacting_particle];
    Vec3 force{};
    RECT client_area{};
    GetClientRect(window, &client_area);
    if (interaction_tool == InteractionTool::spring_drag)
    {
        Vec3 cursor_position{};
        if (screen_to_plane(client_area, pointer_position,
                particle.position.z, cursor_position))
        {
            const Vec3 target = cursor_position + molecule_drag_offset;
            const Vec3 displacement = target - particle.position;
            const double damping =
                2.0 * std::sqrt(spring_drag_stiffness * particle.mass);
            force = displacement * spring_drag_stiffness -
                particle.velocity * damping;
        }
    }
    else if (interaction_tool == InteractionTool::directional_force)
    {
        Vec3 start_position{};
        Vec3 current_position{};
        if (screen_to_plane(client_area, force_drag_start_position,
                particle.position.z, start_position) &&
            screen_to_plane(client_area, pointer_position,
                particle.position.z, current_position))
        {
            force = (current_position - start_position) *
                directional_force_scale;
        }
    }

    force = limit_interaction_force(force);
    if (!simulation_engine.set_external_force(interacting_particle, force))
    {
        MessageBoxW(window, L"Unable to apply the interaction force.",
            L"Simulation error", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void stop_molecule_interaction(HWND window)
{
    const std::size_t active_index = interacting_particle;
    molecule_dragging = false;
    interacting_particle = static_cast<std::size_t>(-1);
    if (active_index < simulation_engine.particles().size() &&
        !simulation_engine.set_external_force(active_index, Vec3{}))
    {
        MessageBoxW(window, L"Unable to release the applied molecule force.",
            L"Simulation error", MB_OK | MB_ICONERROR);
    }
    invalidate_simulation_ui(window, true);
}

std::vector<std::size_t> current_selection()
{
    if (!selected_particles.empty())
    {
        return selected_particles;
    }
    if (selected_molecule_id > 0 &&
        selected_molecule_id <= simulation_engine.particles().size())
    {
        return {static_cast<std::size_t>(selected_molecule_id - 1)};
    }
    return {};
}

void capture_chart_particle_selection()
{
    chart_particle_indices.clear();
    const std::vector<std::size_t> selection = current_selection();
    for (const std::size_t index : selection)
    {
        if (index < simulation_engine.particles().size() &&
            chart_particle_indices.size() < maximum_chart_particles)
        {
            chart_particle_indices.push_back(index);
        }
    }
    if (chart_particle_indices.empty())
    {
        const auto& particles = simulation_engine.particles();
        for (std::size_t index = 0;
            index < particles.size() &&
                chart_particle_indices.size() < maximum_chart_particles;
            ++index)
        {
            chart_particle_indices.push_back(index);
        }
    }
    chart_selection_initialized = true;
}

double sample_particle_quantity(const ChartSample& sample,
    std::size_t particle_slot, std::size_t quantity)
{
    if (quantity == 0)
    {
        return sample.particle_values[particle_slot][0];
    }
    if (quantity >= 2 && quantity <= 8)
    {
        return sample.particle_values[particle_slot][quantity - 1];
    }
    return 0.0;
}

double chart_sample_value(std::size_t sample_index,
    std::size_t particle_slot, std::size_t quantity)
{
    if (sample_index >= chart_sample_count || chart_samples.empty())
    {
        return 0.0;
    }
    const std::size_t oldest =
        (chart_next_sample + chart_sample_capacity - chart_sample_count) %
        chart_sample_capacity;
    const std::size_t ring_index =
        (oldest + sample_index) % chart_sample_capacity;
    const ChartSample& sample = chart_samples[ring_index];
    if (quantity == 1)
    {
        if (sample_index == 0)
        {
            return 0.0;
        }
        const std::size_t previous_index =
            (ring_index + chart_sample_capacity - 1) % chart_sample_capacity;
        const ChartSample& previous = chart_samples[previous_index];
        const double elapsed = sample.time - previous.time;
        return elapsed > 0.0
            ? (sample.particle_values[particle_slot][0] -
                previous.particle_values[particle_slot][0]) / elapsed
            : 0.0;
    }
    if (quantity >= 9)
    {
        return sample.system_values[quantity - 9];
    }
    return sample_particle_quantity(sample, particle_slot, quantity);
}

void record_chart_sample()
{
    if (chart_window == nullptr || !chart_recording ||
        chart_samples.size() != chart_sample_capacity ||
        simulation_engine.metrics().steps % chart_sample_step_stride != 0 ||
        chart_last_recorded_step == simulation_engine.metrics().steps)
    {
        return;
    }

    ChartSample sample;
    const auto& metrics = simulation_engine.metrics();
    const double time_step = simulation_engine.config().time_step;
    sample.time = static_cast<double>(metrics.steps) * time_step;
    sample.system_values = {
        metrics.kinetic_energy,
        metrics.potential_energy,
        metrics.total_energy,
        metrics.temperature,
        metrics.pressure};
    const auto& particles = simulation_engine.particles();
    for (std::size_t slot = 0;
        slot < chart_particle_indices.size() &&
            slot < maximum_chart_particles;
        ++slot)
    {
        const std::size_t index = chart_particle_indices[slot];
        if (index >= particles.size())
        {
            continue;
        }
        const simulation::Particle& particle = particles[index];
        const double speed = std::sqrt(dot(particle.velocity, particle.velocity));
        sample.particle_values[slot] = {
            speed,
            particle.position.x,
            particle.position.y,
            particle.position.z,
            particle.velocity.x,
            particle.velocity.y,
            particle.velocity.z,
            particle.charge};
    }

    chart_samples[chart_next_sample] = sample;
    chart_next_sample = (chart_next_sample + 1) % chart_sample_capacity;
    chart_sample_count = (std::min)(chart_sample_count + 1,
        chart_sample_capacity);
    chart_last_recorded_step = metrics.steps;
    InvalidateRect(chart_window, nullptr, FALSE);
}

constexpr std::size_t chart_quantity_count = 14;
const wchar_t* chart_quantity_labels[chart_quantity_count]{
    L"Скорость", L"Изменение скорости", L"Положение X",
    L"Положение Y", L"Положение Z", L"Скорость Vx",
    L"Скорость Vy", L"Скорость Vz", L"Заряд",
    L"Кинетическая энергия", L"Потенциальная энергия",
    L"Полная энергия", L"Температура", L"Давление"};

void draw_chart_window(HDC context, const RECT& client_area)
{
    fill_rectangle(context, client_area, canvas_color);
    const RECT toolbar{0, 0, client_area.right, 68};
    fill_rectangle(context, toolbar, toolbar_color);
    draw_line(context, 0, toolbar.bottom - 1,
        client_area.right, toolbar.bottom - 1, border_color);
    draw_text(context, L"Показатель",
        RECT{16, 8, 86, 30}, muted_text_color, small_font);
    const RECT quantity_button{16, 33, 226, 59};
    const RECT mode_button{238, 33, 374, 59};
    const RECT record_button{386, 33, 520, 59};
    const RECT clear_button{532, 33, 626, 59};
    const RECT selection_button{638, 33, 782, 59};
    const auto draw_button = [context](const RECT& area,
                                  const wchar_t* label, bool active) {
        fill_rectangle(context, area,
            active ? active_surface_color : surface_color);
        draw_line(context, area.left, area.top, area.right, area.top,
            border_color);
        draw_line(context, area.left, area.bottom - 1,
            area.right, area.bottom - 1, border_color);
        draw_line(context, area.left, area.top, area.left, area.bottom,
            border_color);
        draw_line(context, area.right - 1, area.top,
            area.right - 1, area.bottom, border_color);
        draw_text(context, label, area,
            active ? x_axis_color : primary_text_color,
            small_font, DT_CENTER);
    };
    draw_button(quantity_button, chart_quantity_labels[chart_quantity], false);
    draw_button(mode_button,
        chart_quantity >= 9 ? L"Система" :
            chart_difference_mode ? L"Разность" : L"Отдельные",
        chart_quantity >= 9);
    draw_button(record_button,
        chart_recording ? L"Запись: ВКЛ" : L"Запись: ВЫКЛ",
        chart_recording);
    draw_button(clear_button, L"Очистить", false);
    draw_button(selection_button, L"Обновить выбор", false);

    wchar_t status[96]{};
    std::swprintf(status, std::size(status),
        L"Записей: %llu / %llu    Молекул: %llu",
        static_cast<unsigned long long>(chart_sample_count),
        static_cast<unsigned long long>(chart_sample_capacity),
        static_cast<unsigned long long>(chart_particle_indices.size()));
    draw_text(context, status,
        RECT{800, 28, client_area.right - 16, 58},
        muted_text_color, small_font, DT_RIGHT);

    RECT plot{82, 94, client_area.right - 28, client_area.bottom - 66};
    if (plot.right - plot.left < 100 || plot.bottom - plot.top < 100)
    {
        return;
    }
    fill_rectangle(context, plot, canvas_color);
    constexpr int horizontal_divisions = 5;
    for (int index = 0; index <= horizontal_divisions; ++index)
    {
        const int y = plot.top + (plot.bottom - plot.top) * index /
            horizontal_divisions;
        draw_line(context, plot.left, y, plot.right, y, border_color);
    }
    draw_line(context, plot.left, plot.top, plot.left, plot.bottom,
        border_color);
    draw_line(context, plot.left, plot.bottom, plot.right, plot.bottom,
        border_color);
    draw_text(context, chart_quantity_labels[chart_quantity],
        RECT{16, plot.top, 78, plot.top + 30},
        x_axis_color, small_font, DT_RIGHT);
    if (chart_sample_count == 0)
    {
        draw_text(context, chart_recording
                ? L"Запустите симуляцию, чтобы начать запись графика."
                : L"Включите запись и запустите симуляцию.",
            RECT{plot.left + 12, plot.top + 12,
                plot.right - 12, plot.bottom - 12},
            muted_text_color, status_font, DT_CENTER);
        return;
    }

    const std::size_t oldest =
        (chart_next_sample + chart_sample_capacity - chart_sample_count) %
        chart_sample_capacity;
    const double first_time = chart_samples[oldest].time;
    const double last_time = chart_samples[
        (chart_next_sample + chart_sample_capacity - 1) %
        chart_sample_capacity].time;
    const double time_range = (std::max)(last_time - first_time, 1e-9);

    struct ChartLine
    {
        std::size_t first_slot = 0;
        std::size_t second_slot = 0;
        COLORREF color = x_axis_color;
        wchar_t label[40]{};
    };
    ChartLine lines[maximum_chart_particles]{};
    std::size_t line_count = 0;
    const bool system_quantity = chart_quantity >= 9;
    if (system_quantity)
    {
        lines[0].color = x_axis_color;
        std::swprintf(lines[0].label, std::size(lines[0].label), L"Система");
        line_count = 1;
    }
    else if (chart_difference_mode)
    {
        for (std::size_t slot = 1;
            slot < chart_particle_indices.size() &&
                line_count < maximum_chart_particles;
            ++slot)
        {
            lines[line_count].first_slot = 0;
            lines[line_count].second_slot = slot;
            lines[line_count].color = static_cast<COLORREF>(
                RGB(160 + (slot * 17) % 80, 132 + (slot * 29) % 100,
                    190 + (slot * 11) % 60));
            std::swprintf(lines[line_count].label,
                std::size(lines[line_count].label), L"#%llu − #%llu",
                static_cast<unsigned long long>(
                    chart_particle_indices[0] + 1),
                static_cast<unsigned long long>(
                    chart_particle_indices[slot] + 1));
            ++line_count;
        }
    }
    else
    {
        for (std::size_t slot = 0;
            slot < chart_particle_indices.size() &&
                line_count < maximum_chart_particles;
            ++slot)
        {
            lines[line_count].first_slot = slot;
            lines[line_count].color = static_cast<COLORREF>(
                RGB(69 + (slot * 37) % 150, 132 + (slot * 23) % 100,
                    150 + (slot * 17) % 100));
            std::swprintf(lines[line_count].label,
                std::size(lines[line_count].label), L"Молекула #%llu",
                static_cast<unsigned long long>(
                    chart_particle_indices[slot] + 1));
            ++line_count;
        }
    }

    if (line_count == 0)
    {
        draw_text(context,
            L"Для графика разности выберите минимум две молекулы.",
            RECT{plot.left + 12, plot.top + 12,
                plot.right - 12, plot.bottom - 12},
            muted_text_color, status_font, DT_CENTER);
        return;
    }

    double minimum = 0.0;
    double maximum = 0.0;
    bool has_value = false;
    for (std::size_t sample_index = 0;
        sample_index < chart_sample_count;
        ++sample_index)
    {
        for (std::size_t line_index = 0; line_index < line_count;
            ++line_index)
        {
            double value = chart_sample_value(sample_index,
                lines[line_index].first_slot, chart_quantity);
            if (chart_difference_mode && !system_quantity)
            {
                value -= chart_sample_value(sample_index,
                    lines[line_index].second_slot, chart_quantity);
            }
            if (!has_value)
            {
                minimum = maximum = value;
                has_value = true;
            }
            else
            {
                minimum = (std::min)(minimum, value);
                maximum = (std::max)(maximum, value);
            }
        }
    }
    const double value_range = (std::max)(maximum - minimum, 1e-9);
    const double vertical_margin = value_range * 0.08;
    minimum -= vertical_margin;
    maximum += vertical_margin;
    const double adjusted_range = maximum - minimum;
    wchar_t axis_value[32]{};
    for (int index = 0; index <= horizontal_divisions; ++index)
    {
        const double value = maximum -
            adjusted_range * static_cast<double>(index) /
                horizontal_divisions;
        std::swprintf(axis_value, std::size(axis_value), L"%.3g", value);
        const int y = plot.top + (plot.bottom - plot.top) * index /
            horizontal_divisions;
        draw_text(context, axis_value,
            RECT{4, y - 8, plot.left - 8, y + 8},
            muted_text_color, small_font, DT_RIGHT);
    }
    for (std::size_t line_index = 0; line_index < line_count; ++line_index)
    {
        HPEN pen = CreatePen(PS_SOLID, 2, lines[line_index].color);
        HPEN previous_pen = static_cast<HPEN>(SelectObject(context, pen));
        bool previous_valid = false;
        for (std::size_t sample_index = 0;
            sample_index < chart_sample_count;
            ++sample_index)
        {
            const std::size_t ring_index =
                (oldest + sample_index) % chart_sample_capacity;
            const ChartSample& sample = chart_samples[ring_index];
            double value = chart_sample_value(sample_index,
                lines[line_index].first_slot, chart_quantity);
            if (chart_difference_mode && !system_quantity)
            {
                value -= chart_sample_value(sample_index,
                    lines[line_index].second_slot, chart_quantity);
            }
            if (!std::isfinite(value))
            {
                previous_valid = false;
                continue;
            }
            const int x = plot.left + static_cast<int>(
                (sample.time - first_time) / time_range *
                (plot.right - plot.left));
            const int y = plot.bottom - static_cast<int>(
                (value - minimum) / adjusted_range *
                (plot.bottom - plot.top));
            if (previous_valid)
            {
                LineTo(context, x, y);
            }
            else
            {
                MoveToEx(context, x, y, nullptr);
            }
            previous_valid = true;
        }
        SelectObject(context, previous_pen);
        DeleteObject(pen);
    }

    int legend_left = plot.left + 12;
    for (std::size_t index = 0; index < line_count; ++index)
    {
        draw_line(context, legend_left, client_area.bottom - 38,
            legend_left + 22, client_area.bottom - 38,
            lines[index].color, 2);
        draw_text(context, lines[index].label,
            RECT{legend_left + 28, client_area.bottom - 49,
                legend_left + 150, client_area.bottom - 27},
            primary_text_color, small_font);
        legend_left += 165;
        if (legend_left + 160 > client_area.right)
        {
            break;
        }
    }
    wchar_t time_label[64]{};
    std::swprintf(time_label, std::size(time_label),
        L"Время симуляции: %.4f – %.4f", first_time, last_time);
    draw_text(context, time_label,
        RECT{plot.left, client_area.bottom - 26,
            plot.right, client_area.bottom - 6},
        muted_text_color, small_font, DT_RIGHT);
}

LRESULT CALLBACK chart_window_procedure(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC context = BeginPaint(window, &paint);
        RECT client_area{};
        GetClientRect(window, &client_area);
        draw_chart_window(context, client_area);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_LBUTTONDOWN)
    {
        const int x = static_cast<short>(LOWORD(l_param));
        const int y = static_cast<short>(HIWORD(l_param));
        RECT client_area{};
        GetClientRect(window, &client_area);
        if (y >= 33 && y <= 59)
        {
            if (x >= 16 && x < 226)
            {
                chart_quantity =
                    (chart_quantity + 1) % chart_quantity_count;
            }
            else if (x >= 238 && x < 374)
            {
                if (chart_quantity < 9)
                {
                    chart_difference_mode = !chart_difference_mode;
                }
            }
            else if (x >= 386 && x < 520)
            {
                chart_recording = !chart_recording;
            }
            else if (x >= 532 && x < 626)
            {
                chart_sample_count = 0;
                chart_next_sample = 0;
                chart_last_recorded_step = 0;
            }
            else if (x >= 638 && x < 782)
            {
                capture_chart_particle_selection();
                chart_sample_count = 0;
                chart_next_sample = 0;
                chart_last_recorded_step = 0;
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        limits->ptMinTrackSize.x = 850;
        limits->ptMinTrackSize.y = 420;
        return 0;
    }
    if (message == WM_CLOSE)
    {
        chart_recording = false;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY && chart_window == window)
    {
        chart_window = nullptr;
        chart_recording = false;
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

RECT element_table_cell_area(const RECT& client, int row, int column)
{
    constexpr int cell_width = 48;
    constexpr int cell_height = 44;
    constexpr int gap = 4;
    constexpr int step_x = cell_width + gap;
    constexpr int step_y = cell_height + 6;
    const int table_width = 18 * cell_width + 17 * gap;
    const int left = (static_cast<int>(client.right) - table_width) / 2;
    const int top = 88;
    return RECT{left + column * step_x, top + row * step_y,
        left + column * step_x + cell_width,
        top + row * step_y + cell_height};
}

bool element_matches_search(const elements::Element& element,
    const wchar_t* search_text)
{
    if (search_text == nullptr || search_text[0] == L'\0')
    {
        return true;
    }
    std::wstring query(search_text);
    std::wstring name(element.name);
    std::wstring symbol(element.symbol);
    CharLowerBuffW(&query[0], static_cast<DWORD>(query.size()));
    CharLowerBuffW(&name[0], static_cast<DWORD>(name.size()));
    CharLowerBuffW(&symbol[0], static_cast<DWORD>(symbol.size()));
    wchar_t number[8]{};
    std::swprintf(number, std::size(number), L"%d", element.atomic_number);
    return name.find(query) != std::wstring::npos ||
        symbol.find(query) != std::wstring::npos ||
        query == number;
}

void accept_periodic_table_selection(HWND window)
{
    selected_element_number = highlighted_element_number;
    if (element_table_window == window)
    {
        DestroyWindow(window);
    }
    if (main_window != nullptr)
    {
        InvalidateRect(main_window, nullptr, FALSE);
    }
    if (detached_interface_window != nullptr)
    {
        InvalidateRect(detached_interface_window, nullptr, FALSE);
    }
}

LRESULT CALLBACK element_table_window_procedure(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }
    if (message == WM_CREATE)
    {
        element_search_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            24, 18, 320, 28, window, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (element_search_edit == nullptr)
        {
            MessageBoxW(window, L"Не удалось создать поле поиска элементов.",
                L"Таблица элементов", MB_OK | MB_ICONERROR);
            return -1;
        }
        element_search_background = CreateSolidBrush(surface_color);
        if (element_search_background == nullptr)
        {
            DestroyWindow(element_search_edit);
            element_search_edit = nullptr;
            MessageBoxW(window, L"Не удалось оформить поле поиска элементов.",
                L"Таблица элементов", MB_OK | MB_ICONERROR);
            return -1;
        }
        SendMessageW(element_search_edit, WM_SETFONT,
            reinterpret_cast<WPARAM>(small_font), TRUE);
        return 0;
    }
    if (message == WM_CTLCOLOREDIT)
    {
        SetTextColor(reinterpret_cast<HDC>(w_param), primary_text_color);
        SetBkColor(reinterpret_cast<HDC>(w_param), surface_color);
        return reinterpret_cast<LRESULT>(element_search_background);
    }
    if (message == WM_SIZE)
    {
        if (element_search_edit != nullptr)
        {
            SetWindowPos(element_search_edit, nullptr, 24, 18, 320, 28,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        limits->ptMinTrackSize.x = 1000;
        limits->ptMinTrackSize.y = 730;
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(w_param) == EN_CHANGE)
    {
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_LBUTTONDBLCLK || message == WM_LBUTTONDOWN)
    {
        const POINT point{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        RECT client{};
        GetClientRect(window, &client);
        const RECT use_button{client.right - 190, client.bottom - 58,
            client.right - 24, client.bottom - 18};
        if (PtInRect(&use_button, point))
        {
            accept_periodic_table_selection(window);
            return 0;
        }
        wchar_t query[64]{};
        if (element_search_edit != nullptr)
        {
            GetWindowTextW(element_search_edit, query,
                static_cast<int>(std::size(query)));
        }
        for (const elements::Element& element : elements::all())
        {
            if (!element_matches_search(element, query))
            {
                continue;
            }
            const elements::Position grid =
                elements::table_position(element.atomic_number);
            const RECT cell = element_table_cell_area(
                client, grid.row, grid.column);
            if (PtInRect(&cell, point))
            {
                highlighted_element_number = element.atomic_number;
                if (message == WM_LBUTTONDBLCLK)
                {
                    accept_periodic_table_selection(window);
                }
                else
                {
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
        }
        return 0;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        HDC context = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        fill_rectangle(context, client, canvas_color);
        wchar_t query[64]{};
        if (element_search_edit != nullptr)
        {
            GetWindowTextW(element_search_edit, query,
                static_cast<int>(std::size(query)));
        }
        draw_text(context, L"118 ЭЛЕМЕНТОВ · ПОИСК ПО НАЗВАНИЮ, СИМВОЛУ И НОМЕРУ",
            RECT{360, 18, client.right - 24, 46},
            muted_text_color, small_font);
        draw_text(context, L"ПЕРИОДИЧЕСКАЯ ТАБЛИЦА",
            RECT{24, 54, client.right - 24, 78},
            primary_text_color, button_font);
        for (const elements::Element& element : elements::all())
        {
            if (!element_matches_search(element, query))
            {
                continue;
            }
            const elements::Position grid =
                elements::table_position(element.atomic_number);
            const RECT cell = element_table_cell_area(
                client, grid.row, grid.column);
            const bool active =
                highlighted_element_number == element.atomic_number;
            fill_rectangle(context, cell,
                active ? active_surface_color : surface_color);
            draw_line(context, cell.left, cell.top, cell.right, cell.top,
                active ? x_axis_color : border_color, active ? 2 : 1);
            draw_line(context, cell.left, cell.bottom - 1,
                cell.right, cell.bottom - 1, border_color);
            draw_line(context, cell.left, cell.top, cell.left,
                cell.bottom, border_color);
            draw_line(context, cell.right - 1, cell.top,
                cell.right - 1, cell.bottom, border_color);
            const COLORREF color = element_color(element.atomic_number);
            wchar_t number[8]{};
            std::swprintf(number, std::size(number), L"%d",
                element.atomic_number);
            draw_text(context, number,
                RECT{cell.left + 3, cell.top + 2,
                    cell.right - 3, cell.top + 13},
                muted_text_color, small_font);
            draw_text(context, element.symbol,
                RECT{cell.left + 2, cell.top + 13,
                    cell.right - 2, cell.bottom - 2},
                color, button_font, DT_CENTER);
        }

        const elements::Element* selected =
            elements::find(highlighted_element_number);
        if (selected != nullptr)
        {
            const int detail_top = 568;
            fill_rectangle(context,
                RECT{24, detail_top, client.right - 24,
                    client.bottom - 70}, toolbar_color);
            draw_text(context, selected->symbol,
                RECT{40, detail_top + 10, 112, detail_top + 70},
                element_color(selected->atomic_number), title_font, DT_CENTER);
            wchar_t title[128]{};
            std::swprintf(title, std::size(title), L"%d · %s",
                selected->atomic_number, selected->name);
            draw_text(context, title,
                RECT{132, detail_top + 8, client.right - 210, detail_top + 32},
                primary_text_color, button_font);
            wchar_t properties[192]{};
            std::swprintf(properties, std::size(properties),
                L"Масса: %.5g а.е.м.     Ковалентный радиус: %.0f пм     %s",
                selected->atomic_mass, selected->covalent_radius_pm,
                elements::category_name(selected->category));
            draw_text(context, properties,
                RECT{132, detail_top + 36, client.right - 210,
                    detail_top + 64},
                muted_text_color, small_font);
        }
        const RECT use_button{client.right - 190, client.bottom - 58,
            client.right - 24, client.bottom - 18};
        fill_rectangle(context, use_button, active_surface_color);
        draw_text(context, L"ИСПОЛЬЗОВАТЬ",
            RECT{use_button.left, use_button.top,
                use_button.right, use_button.bottom},
            x_axis_color, button_font, DT_CENTER);
        draw_text(context,
            L"Классическая модель: параметры взаимодействия приближённые",
            RECT{24, client.bottom - 56, client.right - 210,
                client.bottom - 20},
            muted_text_color, small_font);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        if (element_table_window == window)
        {
            element_table_window = nullptr;
            element_search_edit = nullptr;
            if (element_search_background != nullptr)
            {
                DeleteObject(element_search_background);
                element_search_background = nullptr;
            }
        }
        return 0;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void show_periodic_table(HWND owner)
{
    if (element_table_window != nullptr)
    {
        ShowWindow(element_table_window, SW_SHOW);
        SetForegroundWindow(element_table_window);
        return;
    }
    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_DBLCLKS;
        window_class.lpfnWndProc = element_table_window_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = element_table_window_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(owner, L"Не удалось зарегистрировать таблицу элементов.",
                L"Таблица элементов", MB_OK | MB_ICONERROR);
            return;
        }
        class_registered = true;
    }
    highlighted_element_number = selected_element_number;
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    element_table_window = CreateWindowExW(WS_EX_TOOLWINDOW,
        element_table_window_class_name, L"Bonderium — периодическая таблица",
        WS_OVERLAPPEDWINDOW, owner_rect.left + 40, owner_rect.top + 40,
        1120, 790, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (element_table_window == nullptr)
    {
        MessageBoxW(owner, L"Не удалось открыть таблицу элементов.",
            L"Таблица элементов", MB_OK | MB_ICONERROR);
        return;
    }
    ShowWindow(element_table_window, SW_SHOW);
    UpdateWindow(element_table_window);
}

void show_chart_window(HWND owner)
{
    if (chart_window != nullptr)
    {
        ShowWindow(chart_window, SW_SHOW);
        SetForegroundWindow(chart_window);
        return;
    }

    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = chart_window_procedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor =
            LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = chart_window_class_name;
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            MessageBoxW(owner, L"Не удалось зарегистрировать окно графиков.",
                L"Графики", MB_OK | MB_ICONERROR);
            return;
        }
        class_registered = true;
    }

    if (!chart_selection_initialized)
    {
        capture_chart_particle_selection();
    }

    if (chart_samples.size() != chart_sample_capacity)
    {
        chart_samples.resize(chart_sample_capacity);
        chart_sample_count = 0;
        chart_next_sample = 0;
        chart_last_recorded_step = 0;
    }
    chart_recording = true;
    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    chart_window = CreateWindowExW(0, chart_window_class_name,
        L"Bonderium — графики симуляции",
        WS_OVERLAPPEDWINDOW,
        owner_rect.left + 80, owner_rect.top + 60, 1000, 650,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (chart_window == nullptr)
    {
        chart_recording = false;
        MessageBoxW(owner, L"Не удалось создать окно графиков.",
            L"Графики", MB_OK | MB_ICONERROR);
        return;
    }
    ShowWindow(chart_window, SW_SHOW);
    UpdateWindow(chart_window);
}

void toggle_selected_particles_fixed(HWND window)
{
    const std::vector<std::size_t> selection = current_selection();
    if (selection.empty())
    {
        MessageBoxW(window, L"Сначала выберите одну или несколько молекул.",
            L"Заморозка молекул", MB_OK | MB_ICONINFORMATION);
        return;
    }
    bool should_freeze = false;
    for (const std::size_t index : selection)
    {
        if (index < simulation_engine.particles().size() &&
            !simulation_engine.particles()[index].fixed)
        {
            should_freeze = true;
            break;
        }
    }
    for (const std::size_t index : selection)
    {
        if (!simulation_engine.set_particle_fixed(index, should_freeze))
        {
            MessageBoxW(window, L"Не удалось изменить состояние молекулы.",
                L"Ошибка симуляции", MB_OK | MB_ICONERROR);
            return;
        }
    }
    invalidate_simulation_ui(window, true);
}

void select_or_toggle_particle(HWND window, std::uint64_t molecule_id)
{
    if (molecule_id == 0)
    {
        selected_molecule_id = 0;
        selected_particles.clear();
        invalidate_simulation_ui(window, false);
        return;
    }
    const std::size_t index = static_cast<std::size_t>(molecule_id - 1);
    selected_molecule_id = molecule_id;
    if (molecular_tool == MolecularTool::multi_select)
    {
        const auto selected = std::find(
            selected_particles.begin(), selected_particles.end(), index);
        if (selected == selected_particles.end())
        {
            selected_particles.push_back(index);
        }
        else
        {
            selected_particles.erase(selected);
            selected_molecule_id = selected_particles.empty()
                ? 0 : static_cast<std::uint64_t>(selected_particles.back() + 1);
        }
    }
    else
    {
        selected_particles.clear();
    }
    invalidate_simulation_ui(window, false);
}

bool parse_input_number(const wchar_t* text, double& value)
{
    wchar_t* end = nullptr;
    value = std::wcstod(text, &end);
    return end != text && *end == L'\0' && std::isfinite(value);
}

std::vector<std::size_t> experiment_targets()
{
    std::vector<std::size_t> targets = current_selection();
    if (!targets.empty())
    {
        return targets;
    }
    targets.resize(simulation_engine.particles().size());
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        targets[index] = index;
    }
    return targets;
}

bool initialize_thermal_velocities(
    const std::vector<std::size_t>& targets, double temperature)
{
    std::vector<std::size_t> movable;
    for (const std::size_t index : targets)
    {
        if (index < simulation_engine.particles().size() &&
            !simulation_engine.particles()[index].fixed)
        {
            movable.push_back(index);
        }
    }
    if (movable.empty())
    {
        return false;
    }
    if (temperature == 0.0)
    {
        for (const std::size_t index : movable)
        {
            simulation_engine.set_velocity(index, {});
        }
        return true;
    }

    static std::mt19937_64 generator(
        static_cast<std::uint64_t>(GetTickCount64()) ^
        static_cast<std::uint64_t>(GetCurrentProcessId()));
    Vec3 momentum{};
    double total_mass = 0.0;
    for (const std::size_t index : movable)
    {
        const simulation::Particle& particle =
            simulation_engine.particles()[index];
        const double standard_deviation =
            std::sqrt(temperature / particle.mass);
        std::normal_distribution<double> distribution(
            0.0, standard_deviation);
        const Vec3 velocity{
            distribution(generator), distribution(generator),
            simulation_engine.config().two_dimensional
                ? 0.0 : distribution(generator)};
        if (!simulation_engine.set_velocity(index, velocity))
        {
            return false;
        }
        momentum = momentum + velocity * particle.mass;
        total_mass += particle.mass;
    }
    if (movable.size() > 1 && total_mass > 0.0)
    {
        const Vec3 center_velocity = momentum * (1.0 / total_mass);
        for (const std::size_t index : movable)
        {
            const Vec3 velocity =
                simulation_engine.particles()[index].velocity -
                center_velocity;
            if (!simulation_engine.set_velocity(index, velocity))
            {
                return false;
            }
        }
    }

    double kinetic_energy = 0.0;
    for (const std::size_t index : movable)
    {
        const simulation::Particle& particle =
            simulation_engine.particles()[index];
        kinetic_energy += 0.5 * particle.mass *
            dot(particle.velocity, particle.velocity);
    }
    const int dimensions =
        simulation_engine.config().two_dimensional ? 2 : 3;
    const std::size_t degrees_of_freedom =
        static_cast<std::size_t>(dimensions) * movable.size();
    if (kinetic_energy <= 0.0 || degrees_of_freedom == 0)
    {
        return false;
    }
    const double scale = std::sqrt(
        temperature * static_cast<double>(degrees_of_freedom) /
        (2.0 * kinetic_energy));
    for (const std::size_t index : movable)
    {
        const Vec3 velocity =
            simulation_engine.particles()[index].velocity * scale;
        if (!simulation_engine.set_velocity(index, velocity))
        {
            return false;
        }
    }
    return true;
}

void apply_selected_element_to_particles(HWND window)
{
    const std::vector<std::size_t> selection = current_selection();
    if (selection.empty())
    {
        MessageBoxW(window, L"Сначала выберите одну или несколько частиц.",
            L"Элемент", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (const std::size_t index : selection)
    {
        const simulation::Particle particle =
            simulation_engine.particles()[index];
        if (!configure_particle_element(index, selected_element_number,
                particle.charge, particle.fixed))
        {
            MessageBoxW(window, L"Не удалось назначить элемент частице.",
                L"Ошибка симуляции", MB_OK | MB_ICONERROR);
            return;
        }
    }
    invalidate_simulation_ui(window, true);
}

void edit_selected_particle_parameters(HWND window)
{
    const std::vector<std::size_t> selection = current_selection();
    if (selection.empty())
    {
        MessageBoxW(window, L"Сначала выберите одну или несколько частиц.",
            L"Параметры частицы", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const simulation::Particle& first =
        simulation_engine.particles()[selection.front()];
    InputDialogState dialog;
    dialog.title = L"Параметры выбранных частиц";
    dialog.field_count = 4;
    dialog.labels[0] = L"Масса модели (0,001–10000):";
    dialog.labels[1] = L"Заряд (-100…100):";
    dialog.labels[2] = L"Параметр sigma (0,01–100):";
    dialog.labels[3] = L"Параметр epsilon (0–100):";
    std::swprintf(dialog.values[0], std::size(dialog.values[0]), L"%.6g",
        first.mass);
    std::swprintf(dialog.values[1], std::size(dialog.values[1]), L"%.6g",
        first.charge);
    std::swprintf(dialog.values[2], std::size(dialog.values[2]), L"%.6g",
        first.sigma);
    std::swprintf(dialog.values[3], std::size(dialog.values[3]), L"%.6g",
        first.epsilon);
    if (!show_input_dialog(window, dialog))
    {
        return;
    }
    double mass = 0.0;
    double charge = 0.0;
    double sigma = 0.0;
    double epsilon = 0.0;
    if (!parse_input_number(dialog.values[0], mass) ||
        mass < 0.001 || mass > 10000.0 ||
        !parse_input_number(dialog.values[1], charge) ||
        std::abs(charge) > 100.0 ||
        !parse_input_number(dialog.values[2], sigma) ||
        sigma < 0.01 || sigma > 100.0 ||
        !parse_input_number(dialog.values[3], epsilon) ||
        epsilon < 0.0 || epsilon > 100.0)
    {
        MessageBoxW(window, L"Проверьте диапазоны параметров модели.",
            L"Некорректные параметры", MB_OK | MB_ICONWARNING);
        return;
    }
    for (const std::size_t index : selection)
    {
        const simulation::Particle particle =
            simulation_engine.particles()[index];
        if (!simulation_engine.set_particle_properties(index, mass, charge,
                sigma, epsilon, particle.fixed))
        {
            MessageBoxW(window, L"Не удалось обновить параметры частицы.",
                L"Ошибка симуляции", MB_OK | MB_ICONERROR);
            return;
        }
    }
    simulation_engine.invalidate_forces();
    invalidate_simulation_ui(window, true);
}

void thermalize_experiment_particles(HWND window)
{
    const std::vector<std::size_t> targets = experiment_targets();
    if (targets.empty())
    {
        MessageBoxW(window, L"В сцене пока нет частиц.",
            L"Тепловой импульс", MB_OK | MB_ICONINFORMATION);
        return;
    }
    InputDialogState dialog;
    dialog.title = L"Тепловая подготовка";
    dialog.field_count = 1;
    dialog.labels[0] = L"Целевая температура модели (0–1000000):";
    std::swprintf(dialog.values[0], std::size(dialog.values[0]), L"1.0");
    if (!show_input_dialog(window, dialog))
    {
        return;
    }
    double temperature = 0.0;
    if (!parse_input_number(dialog.values[0], temperature) ||
        temperature < 0.0 || temperature > 1000000.0)
    {
        MessageBoxW(window, L"Укажите температуру модели от 0 до 1000000.",
            L"Некорректная температура", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!initialize_thermal_velocities(targets, temperature))
    {
        MessageBoxW(window, L"Не удалось задать скорости. "
            L"Проверьте, что выбраны подвижные частицы.",
            L"Тепловая подготовка", MB_OK | MB_ICONWARNING);
        return;
    }
    invalidate_simulation_ui(window, true);
}

void reset_experiment_velocities(HWND window)
{
    const std::vector<std::size_t> targets = experiment_targets();
    if (targets.empty())
    {
        MessageBoxW(window, L"В сцене пока нет частиц.",
            L"Сброс скоростей", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (const std::size_t index : targets)
    {
        if (!simulation_engine.set_velocity(index, {}))
        {
            MessageBoxW(window, L"Не удалось обнулить скорость частицы.",
                L"Ошибка симуляции", MB_OK | MB_ICONERROR);
            return;
        }
    }
    invalidate_simulation_ui(window, true);
}

void invert_experiment_charges(HWND window)
{
    const std::vector<std::size_t> selection = current_selection();
    if (selection.empty())
    {
        MessageBoxW(window, L"Выберите частицы, заряд которых нужно изменить.",
            L"Заряды", MB_OK | MB_ICONINFORMATION);
        return;
    }
    for (const std::size_t index : selection)
    {
        if (!simulation_engine.set_charge(
                index, -simulation_engine.particles()[index].charge))
        {
            MessageBoxW(window, L"Не удалось изменить заряд частицы.",
                L"Ошибка симуляции", MB_OK | MB_ICONERROR);
            return;
        }
    }
    simulation_engine.invalidate_forces();
    invalidate_simulation_ui(window, true);
}

void create_gas_sample(HWND window)
{
    constexpr std::size_t particle_count = 64;
    if (!ram_budget_allows_molecule(window, particle_count))
    {
        return;
    }
    const elements::Element* element =
        elements::find(selected_element_number);
    if (element == nullptr)
    {
        MessageBoxW(window, L"Выбранный элемент недоступен.",
            L"Образец газа", MB_OK | MB_ICONERROR);
        return;
    }
    const double spacing = (std::max)(1.0,
        element_model_sigma(*element) * 1.5);
    const int side = simulation_engine.config().two_dimensional ? 8 : 4;
    const std::size_t first = simulation_engine.particles().size();
    for (int z = 0; z < (simulation_engine.config().two_dimensional ? 1 : 4);
         ++z)
    {
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const Vec3 position{
                    camera_state.target.x +
                        (x - (side - 1) * 0.5) * spacing,
                    camera_state.target.y +
                        (y - (side - 1) * 0.5) * spacing,
                    simulation_engine.config().two_dimensional ? 0.0 :
                        camera_state.target.z +
                            (z - 1.5) * spacing};
                const std::size_t index =
                    simulation_engine.add_particle(position);
                if (!configure_particle_element(index, selected_element_number))
                {
                    MessageBoxW(window,
                        L"Не удалось создать полный образец газа.",
                        L"Ошибка симуляции", MB_OK | MB_ICONERROR);
                    return;
                }
            }
        }
    }
    selected_particles.clear();
    for (std::size_t index = first;
         index < simulation_engine.particles().size(); ++index)
    {
        selected_particles.push_back(index);
    }
    selected_molecule_id = static_cast<std::uint64_t>(
        simulation_engine.particles().size());
    if (!initialize_thermal_velocities(selected_particles, 1.0))
    {
        MessageBoxW(window, L"Образец создан, но не удалось задать "
            L"начальные скорости.",
            L"Образец газа", MB_OK | MB_ICONWARNING);
    }
    invalidate_simulation_ui(window, true);
}

void create_ionic_pair_sample(HWND window)
{
    if (!ram_budget_allows_molecule(window, 2))
    {
        return;
    }
    const elements::Element* element =
        elements::find(selected_element_number);
    if (element == nullptr)
    {
        MessageBoxW(window, L"Выбранный элемент недоступен.",
            L"Ионная пара", MB_OK | MB_ICONERROR);
        return;
    }
    const double separation = (std::max)(1.5,
        element_model_sigma(*element) * 1.5);
    const double center_z = simulation_engine.config().two_dimensional
        ? 0.0 : camera_state.target.z;
    const std::size_t first = simulation_engine.add_particle(Vec3{
        camera_state.target.x - separation * 0.5,
        camera_state.target.y, center_z});
    const std::size_t second = simulation_engine.add_particle(Vec3{
        camera_state.target.x + separation * 0.5,
        camera_state.target.y, center_z});
    if (!configure_particle_element(first, selected_element_number, 1.0) ||
        !configure_particle_element(second, selected_element_number, -1.0))
    {
        MessageBoxW(window, L"Не удалось настроить ионную пару.",
            L"Ошибка симуляции", MB_OK | MB_ICONERROR);
        return;
    }
    selected_particles = {first, second};
    selected_molecule_id = static_cast<std::uint64_t>(second + 1);
    invalidate_simulation_ui(window, true);
}

void configure_square_lattice(HWND window)
{
    InputDialogState dialog;
    dialog.title = L"Квадратная решётка";
    dialog.field_count = 1;
    dialog.labels[0] = L"Количество молекул (1–10000):";
    std::swprintf(dialog.values[0],
        sizeof(dialog.values[0]) / sizeof(dialog.values[0][0]), L"%llu",
        static_cast<unsigned long long>(square_lattice_count));
    if (!show_input_dialog(window, dialog))
    {
        return;
    }
    wchar_t* end = nullptr;
    const unsigned long long count =
        std::wcstoull(dialog.values[0], &end, 10);
    if (end == dialog.values[0] || *end != L'\0' ||
        count == 0 || count > 10000)
    {
        MessageBoxW(window, L"Укажите целое число от 1 до 10000.",
            L"Некорректное количество", MB_OK | MB_ICONWARNING);
        return;
    }
    square_lattice_count = static_cast<std::size_t>(count);
    square_lattice_for_scenario = false;
    molecular_tool = MolecularTool::square_lattice;
    molecule_tool_active = false;
    bond_tool_active = false;
    invalidate_simulation_ui(window, true);
}

void add_scenario_step(HWND window)
{
    const std::vector<std::size_t> targets = current_selection();
    if (targets.empty())
    {
        MessageBoxW(window,
            L"Выберите молекулы, на которые будет действовать сила.",
            L"Конструктор сценария", MB_OK | MB_ICONINFORMATION);
        return;
    }
    InputDialogState dialog;
    dialog.title = L"Шаг сценария: сила";
    dialog.field_count = 4;
    dialog.labels[0] = L"Длительность шага, секунд (0.01–3600):";
    dialog.labels[1] = L"Сила по X (-200…200):";
    dialog.labels[2] = L"Сила по Y (-200…200):";
    dialog.labels[3] = L"Сила по Z (-200…200):";
    std::swprintf(dialog.values[0],
        sizeof(dialog.values[0]) / sizeof(dialog.values[0][0]), L"1.0");
    for (std::size_t index = 1; index < dialog.field_count; ++index)
    {
        std::swprintf(dialog.values[index],
            sizeof(dialog.values[index]) / sizeof(dialog.values[index][0]),
            L"0");
    }
    if (!show_input_dialog(window, dialog))
    {
        return;
    }
    double duration = 0.0;
    Vec3 force{};
    if (!parse_input_number(dialog.values[0], duration) ||
        duration < 0.01 || duration > 3600.0 ||
        !parse_input_number(dialog.values[1], force.x) ||
        !parse_input_number(dialog.values[2], force.y) ||
        !parse_input_number(dialog.values[3], force.z) ||
        std::abs(force.x) > maximum_interaction_force ||
        std::abs(force.y) > maximum_interaction_force ||
        std::abs(force.z) > maximum_interaction_force)
    {
        MessageBoxW(window,
            L"Проверьте длительность и компоненты силы. "
            L"Длительность: 0.01–3600 секунд, каждая компонента: ±200.",
            L"Некорректный шаг сценария", MB_OK | MB_ICONWARNING);
        return;
    }
    ScenarioStep step;
    step.action = ScenarioAction::apply_force;
    step.duration_seconds = duration;
    step.force = force;
    step.targets = targets;
    scenario_steps.push_back(std::move(step));
    invalidate_simulation_ui(window, false);
}

bool create_square_lattice(HWND window, std::size_t count,
    Vec3 start, Vec3 end)
{
    if (!ram_budget_allows_molecule(window, count))
    {
        return false;
    }
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double side = (std::max)(std::abs(dx), std::abs(dy));
    if (side < 0.1)
    {
        return false;
    }
    const std::size_t columns = static_cast<std::size_t>(
        std::ceil(std::sqrt(static_cast<double>(count))));
    const std::size_t rows = (count + columns - 1) / columns;
    const double spacing = side /
        static_cast<double>((std::max)(columns, rows));
    const double end_x = start.x + (dx < 0.0 ? -side : side);
    const double end_y = start.y + (dy < 0.0 ? -side : side);
    const double center_x = (start.x + end_x) * 0.5;
    const double center_y = (start.y + end_y) * 0.5;
    const double start_x = center_x -
        spacing * static_cast<double>(columns - 1) * 0.5;
    const double start_y = center_y -
        spacing * static_cast<double>(rows - 1) * 0.5;
    const std::size_t initial_particle_count =
        simulation_engine.particles().size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t column = index % columns;
        const std::size_t row = index / columns;
        const Vec3 position{
            start_x + static_cast<double>(column) * spacing,
            start_y + static_cast<double>(row) * spacing,
            start.z};
        const std::size_t particle_index =
            simulation_engine.add_particle(position);
        if (!configure_particle_element(
                particle_index, selected_element_number))
        {
            const std::size_t added =
                simulation_engine.particles().size() -
                initial_particle_count;
            wchar_t message[160]{};
            std::swprintf(message, sizeof(message) / sizeof(message[0]),
                L"Не удалось настроить молекулу. Добавлено частиц: %llu из %llu.",
                static_cast<unsigned long long>(added),
                static_cast<unsigned long long>(count));
            MessageBoxW(window, message, L"Ошибка симуляции",
                MB_OK | MB_ICONERROR);
            return false;
        }
    }
    selected_particles.clear();
    selected_molecule_id = static_cast<std::uint64_t>(
        simulation_engine.particles().size());
    return true;
}

void configure_scenario_lattice(HWND window)
{
    InputDialogState dialog;
    dialog.title = L"Шаг сценария: решётка";
    dialog.field_count = 2;
    dialog.labels[0] = L"Количество молекул (1–10000):";
    dialog.labels[1] = L"Задержка шага, секунд (0.01–3600):";
    std::swprintf(dialog.values[0],
        sizeof(dialog.values[0]) / sizeof(dialog.values[0][0]), L"%llu",
        static_cast<unsigned long long>(square_lattice_count));
    std::swprintf(dialog.values[1],
        sizeof(dialog.values[1]) / sizeof(dialog.values[1][0]), L"1.0");
    if (!show_input_dialog(window, dialog))
    {
        return;
    }
    wchar_t* end = nullptr;
    const unsigned long long count =
        std::wcstoull(dialog.values[0], &end, 10);
    double duration = 0.0;
    if (end == dialog.values[0] || *end != L'\0' ||
        count == 0 || count > 10000 ||
        !parse_input_number(dialog.values[1], duration) ||
        duration < 0.01 || duration > 3600.0)
    {
        MessageBoxW(window,
            L"Количество должно быть 1–10000, задержка — 0.01–3600 секунд.",
            L"Некорректный шаблон", MB_OK | MB_ICONWARNING);
        return;
    }
    square_lattice_count = static_cast<std::size_t>(count);
    pending_lattice_duration = duration;
    square_lattice_for_scenario = true;
    molecular_tool = MolecularTool::square_lattice;
    molecule_tool_active = false;
    bond_tool_active = false;
    invalidate_simulation_ui(window, true);
}

void show_scenario_step_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        MessageBoxW(window, L"Не удалось открыть меню сценария.",
            L"Конструктор сценария", MB_OK | MB_ICONERROR);
        return;
    }
    const bool menu_ready =
        AppendMenuW(menu, MF_STRING, menu_scenario_force_step,
            L"Шаг с внешней силой") &&
        AppendMenuW(menu, MF_STRING, menu_scenario_lattice_step,
            L"Шаг создания решётки");
    if (!menu_ready)
    {
        DestroyMenu(menu);
        MessageBoxW(window, L"Не удалось создать меню сценария.",
            L"Конструктор сценария", MB_OK | MB_ICONERROR);
        return;
    }
    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT button = tools_panel_button_area(client_area, 3);
    POINT menu_position{button.left, button.bottom};
    ClientToScreen(window, &menu_position);
    SetForegroundWindow(window);
    const UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        menu_position.x, menu_position.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (command == menu_scenario_force_step)
    {
        add_scenario_step(window);
    }
    else if (command == menu_scenario_lattice_step)
    {
        configure_scenario_lattice(window);
    }
}

void update_scenario(HWND window, double elapsed)
{
    if (!scenario_running)
    {
        return;
    }
    scenario_elapsed_seconds += elapsed;
    double step_end = 0.0;
    std::size_t next_step = scenario_steps.size();
    for (std::size_t index = 0; index < scenario_steps.size(); ++index)
    {
        step_end += scenario_steps[index].duration_seconds;
        if (scenario_elapsed_seconds < step_end)
        {
            next_step = index;
            break;
        }
    }
    if (next_step == active_scenario_step)
    {
        return;
    }
    if (active_scenario_step < scenario_steps.size() &&
        scenario_steps[active_scenario_step].action ==
            ScenarioAction::apply_force)
    {
        for (const std::size_t target :
            scenario_steps[active_scenario_step].targets)
        {
            if (target < simulation_engine.particles().size() &&
                !simulation_engine.set_external_force(target, Vec3{}))
            {
                MessageBoxW(window,
                    L"Не удалось переключить силу сценария.",
                    L"Ошибка сценария", MB_OK | MB_ICONERROR);
                stop_scenario(window);
                return;
            }
        }
    }
    for (std::size_t index = active_scenario_step + 1;
        index < next_step; ++index)
    {
        const ScenarioStep& skipped_step = scenario_steps[index];
        if (skipped_step.action == ScenarioAction::spawn_lattice &&
            !create_square_lattice(window, skipped_step.lattice_count,
                skipped_step.lattice_start, skipped_step.lattice_end))
        {
            stop_scenario(window);
            return;
        }
    }
    active_scenario_step = next_step;
    if (active_scenario_step >= scenario_steps.size())
    {
        scenario_running = false;
        scenario_elapsed_seconds = 0.0;
        active_scenario_step = static_cast<std::size_t>(-1);
        invalidate_simulation_ui(window, false);
        return;
    }
    const ScenarioStep& step = scenario_steps[active_scenario_step];
    if (step.action == ScenarioAction::spawn_lattice)
    {
        if (!create_square_lattice(window, step.lattice_count,
                step.lattice_start, step.lattice_end))
        {
            stop_scenario(window);
            return;
        }
    }
    else
    {
        for (const std::size_t target : step.targets)
        {
            if (target < simulation_engine.particles().size() &&
                !simulation_engine.set_external_force(target, step.force))
            {
                MessageBoxW(window,
                    L"Не удалось применить силу шага сценария.",
                    L"Ошибка сценария", MB_OK | MB_ICONERROR);
                stop_scenario(window);
                return;
            }
        }
    }
    invalidate_simulation_ui(window, false);
}

void stop_scenario(HWND window)
{
    bool release_failed = false;
    if (active_scenario_step < scenario_steps.size() &&
        scenario_steps[active_scenario_step].action ==
            ScenarioAction::apply_force)
    {
        for (const std::size_t target :
            scenario_steps[active_scenario_step].targets)
        {
            if (target < simulation_engine.particles().size() &&
                !simulation_engine.set_external_force(target, Vec3{}))
            {
                release_failed = true;
            }
        }
    }
    scenario_running = false;
    scenario_elapsed_seconds = 0.0;
    active_scenario_step = static_cast<std::size_t>(-1);
    if (release_failed)
    {
        MessageBoxW(window,
            L"Не удалось снять одну или несколько сил сценария.",
            L"Ошибка сценария", MB_OK | MB_ICONERROR);
    }
    invalidate_simulation_ui(window, false);
}

void toggle_scenario(HWND window)
{
    if (scenario_running)
    {
        stop_scenario(window);
    }
    else if (scenario_steps.empty())
    {
        MessageBoxW(window, L"Добавьте хотя бы один шаг сценария.",
            L"Конструктор сценария", MB_OK | MB_ICONINFORMATION);
    }
    else if (!simulation_running)
    {
        MessageBoxW(window, L"Сначала запустите симуляцию.",
            L"Конструктор сценария", MB_OK | MB_ICONINFORMATION);
    }
    else if (molecule_dragging)
    {
        MessageBoxW(window,
            L"Завершите взаимодействие с молекулой перед запуском сценария.",
            L"Конструктор сценария", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        scenario_running = true;
        scenario_elapsed_seconds = 0.0;
        active_scenario_step = static_cast<std::size_t>(-1);
    }
    invalidate_simulation_ui(window, false);
}

void clear_scenario(HWND window)
{
    stop_scenario(window);
    scenario_steps.clear();
    invalidate_simulation_ui(window, false);
}

bool place_square_lattice(HWND window)
{
    return create_square_lattice(window, square_lattice_count,
        square_lattice_start, square_lattice_end);
}

void update_simulation(HWND window)
{
    const ULONGLONG now = GetTickCount64();
    const double elapsed = (std::min)(0.05,
        static_cast<double>(now - last_simulation_tick) / 1000.0);
    last_simulation_tick = now;
    const double time_step = simulation_engine.config().time_step;
    const unsigned int maximum_steps_this_tick =
        static_cast<unsigned int>(std::ceil(
            maximum_catch_up_steps * time_flow_factor));
    simulation_time_accumulator = (std::min)(
        simulation_time_accumulator + elapsed * time_flow_factor,
        time_step * maximum_steps_this_tick);

    unsigned int completed_steps = 0;
    std::string error;
    if (molecule_dragging && !update_interaction_force(window))
    {
        set_simulation_running(window, false);
        return;
    }
    while (simulation_time_accumulator >= time_step &&
        completed_steps < maximum_steps_this_tick)
    {
        update_scenario(window, time_step);
        if (!simulation_engine.step(error))
        {
            set_simulation_running(window, false);
            const std::wstring message(error.begin(), error.end());
            MessageBoxW(window, message.c_str(),
                L"Simulation stopped — invalid state",
                MB_OK | MB_ICONERROR);
            return;
        }
        simulation_time_accumulator -= time_step;
        ++completed_steps;
        record_chart_sample();
    }

    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT area = scene_area(client_area);
    InvalidateRect(window, &area, FALSE);
}

void adjust_selected_charge(HWND window, double delta)
{
    if (selected_molecule_id == 0 ||
        selected_molecule_id > simulation_engine.particles().size())
    {
        MessageBoxW(window, L"Select a molecule before changing its charge.",
            L"Bonderium", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::size_t index =
        static_cast<std::size_t>(selected_molecule_id - 1);
    const double charge =
        simulation_engine.particles()[index].charge + delta;
    if (!simulation_engine.set_charge(index, charge))
    {
        MessageBoxW(window, L"Unable to update the selected molecule charge.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }
    invalidate_simulation_ui(window, true);
}

void adjust_target_temperature(HWND window, double delta)
{
    simulation_engine.config().target_temperature = (std::clamp)(
        simulation_engine.config().target_temperature + delta, 0.0, 10.0);
    invalidate_simulation_ui(window, true);
}

void select_bond_endpoint(HWND window, POINT screen)
{
    RECT client_area{};
    GetClientRect(window, &client_area);
    const std::uint64_t molecule_id = pick_molecule(client_area, screen);
    if (molecule_id == 0)
    {
        return;
    }
    selected_molecule_id = molecule_id;
    if (pending_bond_molecule_id == 0)
    {
        pending_bond_molecule_id = molecule_id;
        invalidate_simulation_ui(window, false);
        return;
    }
    if (pending_bond_molecule_id == molecule_id)
    {
        pending_bond_molecule_id = 0;
        invalidate_simulation_ui(window, false);
        return;
    }

    const std::size_t first =
        static_cast<std::size_t>(pending_bond_molecule_id - 1);
    const std::size_t second = static_cast<std::size_t>(molecule_id - 1);
    const auto& particles = simulation_engine.particles();
    const Vec3 displacement =
        particles[first].position - particles[second].position;
    const double equilibrium_length = std::sqrt(dot(displacement, displacement));
    std::string error;
    if (!simulation_engine.add_bond(
            simulation::Bond{first, second, equilibrium_length, 10.0}, error))
    {
        const std::wstring message(error.begin(), error.end());
        MessageBoxW(window, message.c_str(), L"Unable to create bond",
            MB_OK | MB_ICONERROR);
    }
    pending_bond_molecule_id = 0;
    invalidate_simulation_ui(window, false);
}

void update_camera_motion(HWND window)
{
    const ULONGLONG now = GetTickCount64();
    if (camera_state.last_motion_tick == 0)
    {
        camera_state.last_motion_tick = now;
        return;
    }

    const double elapsed = (std::min)(0.05,
        static_cast<double>(now - camera_state.last_motion_tick) / 1000.0);
    camera_state.last_motion_tick = now;

    const int forward = static_cast<int>(is_key_down('W') || is_key_down(VK_UP)) -
        static_cast<int>(is_key_down('S') || is_key_down(VK_DOWN));
    const int sideways = static_cast<int>(is_key_down('D') || is_key_down(VK_RIGHT)) -
        static_cast<int>(is_key_down('A') || is_key_down(VK_LEFT));
    const int vertical = simulation_engine.config().two_dimensional
        ? 0
        : static_cast<int>(is_key_down('E')) -
            static_cast<int>(is_key_down('Q'));
    const double diagonal_scale = forward != 0 && sideways != 0
        ? 0.7071067811865476
        : 1.0;
    const Vec3 forward_direction{
        -std::cos(camera_state.yaw), -std::sin(camera_state.yaw), 0.0};
    const Vec3 right_direction{
        -std::sin(camera_state.yaw), std::cos(camera_state.yaw), 0.0};
    const Vec3 desired_velocity =
        (forward_direction * (forward * diagonal_scale) +
            right_direction * (sideways * diagonal_scale) +
            Vec3{0.0, 0.0, static_cast<double>(vertical)}) * camera_move_speed;
    const double easing = 1.0 - std::exp(-elapsed / camera_ease_seconds);

    camera_state.velocity = camera_state.velocity +
        (desired_velocity - camera_state.velocity) * easing;
    camera_state.target = camera_state.target + camera_state.velocity * elapsed;
    if (simulation_engine.config().two_dimensional)
    {
        camera_state.target.z = 0.0;
    }

    RECT client_area{};
    GetClientRect(window, &client_area);
    const RECT area = scene_area(client_area);
    InvalidateRect(window, &area, FALSE);
    if (forward == 0 && sideways == 0 && vertical == 0 &&
        std::sqrt(dot(camera_state.velocity, camera_state.velocity)) <
            camera_stop_threshold)
    {
        stop_camera_motion(window);
    }
}

void reset_view(HWND window)
{
    camera_state = CameraState{};
    camera_state.yaw = -pi / 4.0;
    camera_state.pitch = pi / 3.0;
    camera_state.distance = camera_default_distance;
    stop_camera_motion(window);
}

void toggle_dimension_mode(HWND window)
{
    const bool enabling_2d =
        !simulation_engine.config().two_dimensional;
    if (enabling_2d)
    {
        saved_3d_yaw = camera_state.yaw;
        saved_3d_pitch = camera_state.pitch;
        saved_3d_camera = true;
    }
    if (!simulation_engine.set_dimension_mode(enabling_2d))
    {
        MessageBoxW(window, L"Не удалось изменить размерность симуляции.",
            L"Размерность", MB_OK | MB_ICONERROR);
        return;
    }
    stop_camera_motion(window);
    wall_tool_active = false;
    wall_dragging = false;
    if (enabling_2d)
    {
        camera_state.target.z = 0.0;
        camera_state.yaw = -pi / 2.0;
        camera_state.pitch = 1.52;
        molecule_placement_height = 0.0;
    }
    else
    {
        camera_state.yaw = saved_3d_camera ? saved_3d_yaw : -pi / 4.0;
        camera_state.pitch = saved_3d_camera ? saved_3d_pitch : pi / 3.0;
        molecule_placement_height = molecule_radius;
    }
    invalidate_simulation_ui(window, true);
}

void place_molecule(HWND window, POINT position)
{
    if (!ram_budget_allows_molecule(window))
    {
        return;
    }
    RECT client_area{};
    GetClientRect(window, &client_area);
    Vec3 world_position{};
    const double placement_z = simulation_engine.config().two_dimensional
        ? 0.0 : molecule_placement_height;
    if (!screen_to_plane(client_area, position, placement_z, world_position))
    {
        return;
    }
    const std::size_t particle_index =
        simulation_engine.add_particle(world_position);
    if (!configure_particle_element(
            particle_index, selected_element_number))
    {
        MessageBoxW(window, L"Unable to initialize molecule physics properties.",
            L"Simulation error", MB_OK | MB_ICONERROR);
        return;
    }
    selected_molecule_id =
        static_cast<std::uint64_t>(particle_index + 1);
    selected_particles.clear();
}

bool begin_molecule_interaction(
    HWND window, const RECT& client_area, POINT screen)
{
    molecule_dragging = false;
    interacting_particle = static_cast<std::size_t>(-1);
    const std::uint64_t molecule_id = pick_molecule(client_area, screen);
    if (molecule_id == 0)
    {
        selected_molecule_id = 0;
        selected_particles.clear();
        return false;
    }

    selected_molecule_id = molecule_id;
    selected_particles.clear();
    if (interaction_tool == InteractionTool::select ||
        !simulation_running)
    {
        return true;
    }
    Vec3 cursor_position{};
    const Vec3 molecule_position = simulation_engine.particles()[
        static_cast<std::size_t>(molecule_id - 1)].position;
    if (screen_to_plane(client_area, screen, molecule_position.z, cursor_position))
    {
        interacting_particle =
            static_cast<std::size_t>(molecule_id - 1);
        if (interacting_particle >= simulation_engine.particles().size())
        {
            interacting_particle = static_cast<std::size_t>(-1);
            MessageBoxW(window,
                L"The selected molecule has no simulation particle.",
                L"Simulation error", MB_OK | MB_ICONERROR);
            return true;
        }
        molecule_drag_offset = molecule_position - cursor_position;
        force_drag_start_position = screen;
        molecule_dragging = true;
    }
    return true;
}

void draw_preview_cursor(HWND window, POINT position)
{
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32515)));
    (void)window;
    (void)position;
}

void record_rendered_frame(LARGE_INTEGER started_at)
{
    LARGE_INTEGER finished_at{};
    QueryPerformanceCounter(&finished_at);
    const double render_time_ms = performance_frequency.QuadPart == 0
        ? 0.0
        : static_cast<double>(finished_at.QuadPart - started_at.QuadPart) *
            1000.0 / static_cast<double>(performance_frequency.QuadPart);
    if (!telemetry.has_frame_sample)
    {
        telemetry.frame_time_ms = render_time_ms;
        telemetry.has_frame_sample = true;
    }
    else
    {
        telemetry.frame_time_ms += (render_time_ms - telemetry.frame_time_ms) * 0.15;
    }

    ++telemetry.frames_since_sample;
    const ULONGLONG now = GetTickCount64();
    if (telemetry.frame_sample_started_at == 0)
    {
        telemetry.frame_sample_started_at = now;
    }
    const ULONGLONG elapsed = now - telemetry.frame_sample_started_at;
    if (elapsed >= 500)
    {
        telemetry.frames_per_second =
            static_cast<double>(telemetry.frames_since_sample) * 1000.0 /
            static_cast<double>(elapsed);
        telemetry.frames_since_sample = 0;
        telemetry.frame_sample_started_at = now;
    }
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_ERASEBKGND)
    {
        return 1;
    }

    if (message == WM_CREATE)
    {
        window_device_context = GetDC(window);
        if (window_device_context == nullptr)
        {
            return -1;
        }

        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
            PFD_SUPPORT_GDI | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        format.iLayerType = PFD_MAIN_PLANE;
        const int pixel_format = ChoosePixelFormat(window_device_context, &format);
        if (pixel_format == 0 ||
            !SetPixelFormat(window_device_context, pixel_format, &format))
        {
            ReleaseDC(window, window_device_context);
            window_device_context = nullptr;
            return -1;
        }

        open_gl_context = wglCreateContext(window_device_context);
        if (open_gl_context == nullptr ||
            !wglMakeCurrent(window_device_context, open_gl_context))
        {
            if (open_gl_context != nullptr)
            {
                wglDeleteContext(open_gl_context);
                open_gl_context = nullptr;
            }
            ReleaseDC(window, window_device_context);
            window_device_context = nullptr;
            return -1;
        }

        glShadeModel(GL_SMOOTH);
        glClearDepth(1.0);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
        wglMakeCurrent(nullptr, nullptr);
        if (!create_overlay_fonts())
        {
            destroy_overlay_fonts();
            wglDeleteContext(open_gl_context);
            open_gl_context = nullptr;
            ReleaseDC(window, window_device_context);
            window_device_context = nullptr;
            return -1;
        }
        return 0;
    }

    if (message == WM_SIZE)
    {
        if (w_param != SIZE_MINIMIZED)
        {
            resize_detached_interface_window(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    if (message == WM_DISPLAYCHANGE)
    {
        if (fullscreen)
        {
            const HMONITOR monitor =
                MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            if (!GetMonitorInfoW(monitor, &monitor_info) ||
                !SetWindowPos(window, HWND_TOP,
                    monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                    monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                    monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER))
            {
                MessageBoxW(window,
                    L"Unable to resize the fullscreen window for the display.",
                    L"Bonderium", MB_OK | MB_ICONERROR);
            }
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }

    if (message == WM_DPICHANGED)
    {
        if (!fullscreen)
        {
            const auto* suggested =
                reinterpret_cast<const RECT*>(l_param);
            if (!SetWindowPos(window, nullptr,
                    suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE))
            {
                MessageBoxW(window, L"Unable to apply the display scaling.",
                    L"Bonderium", MB_OK | MB_ICONERROR);
            }
        }
        destroy_overlay_fonts();
        if (!create_overlay_fonts(static_cast<int>(HIWORD(w_param))))
        {
            destroy_overlay_fonts();
            MessageBoxW(window, L"Unable to recreate the interface fonts.",
                L"Bonderium", MB_OK | MB_ICONERROR);
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }

    if (message == WM_PAINT)
    {
        LARGE_INTEGER render_started_at{};
        QueryPerformanceCounter(&render_started_at);
        PAINTSTRUCT paint{};
        HDC paint_context = BeginPaint(window, &paint);
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        const bool scene_needs_render =
            paint.rcPaint.left < area.right &&
            paint.rcPaint.right > area.left &&
            paint.rcPaint.top < area.bottom &&
            paint.rcPaint.bottom > area.top;

        const bool scene_rendered = scene_needs_render &&
            window_device_context != nullptr && open_gl_context != nullptr &&
            wglMakeCurrent(window_device_context, open_gl_context);
        if (scene_rendered)
        {
            render_scene(client_area);
            if (!SwapBuffers(window_device_context))
            {
                wglMakeCurrent(nullptr, nullptr);
                EndPaint(window, &paint);
                MessageBoxW(window,
                    L"Unable to present the 3D scene. Try Performance mode in OPT.",
                    L"Bonderium — graphics error",
                    MB_OK | MB_ICONERROR);
                return 0;
            }
            wglMakeCurrent(nullptr, nullptr);
        }
        else if (window_device_context == nullptr || open_gl_context == nullptr)
        {
            fill_rectangle(paint_context, client_area, canvas_color);
            const wchar_t* error =
                L"Unable to initialize the 3D graphics context.";
            TextOutW(paint_context, 24, 24, error,
                static_cast<int>(std::wcslen(error)));
        }

        if (scene_rendered)
        {
            HDC overlay_context = GetDC(window);
            if (overlay_context != nullptr)
            {
                draw_overlay(overlay_context, client_area);
                ReleaseDC(window, overlay_context);
            }
        }
        else if (!separate_interface_windows)
        {
            if (toolbar_visible &&
                rectangles_intersect(paint.rcPaint, toolbar_area(client_area)))
            {
                draw_toolbar_overlay(paint_context, client_area);
            }
            if (footer_visible &&
                rectangles_intersect(paint.rcPaint, footer_area(client_area)))
            {
                draw_footer_overlay(paint_context, client_area);
            }
            if (tools_panel_visible &&
                rectangles_intersect(
                    paint.rcPaint, tools_panel_area(client_area)))
            {
                draw_tools_panel_overlay(paint_context, client_area);
            }
            if (rectangles_intersect(paint.rcPaint,
                    RECT{client_area.left,
                        client_area.top + toolbar_height,
                        client_area.left + workspace_rail_width,
                        client_area.bottom}))
            {
                draw_workspace_rail(paint_context, client_area);
            }
        }
        EndPaint(window, &paint);
        if (scene_rendered)
        {
            record_rendered_frame(render_started_at);
        }
        return 0;
    }

    if (message == WM_KEYDOWN)
    {
        if (w_param == VK_F11 && (l_param & (1L << 30)) == 0)
        {
            if (!separate_interface_windows)
            {
                toggle_fullscreen(window);
            }
            return 0;
        }
        if (w_param == VK_F2 && (l_param & (1L << 30)) == 0)
        {
            show_panels_menu(window);
            return 0;
        }
        if (w_param == VK_F1 && (l_param & (1L << 30)) == 0)
        {
            show_help(window);
            return 0;
        }
        if (w_param == VK_ESCAPE && (molecule_tool_active || bond_tool_active ||
                molecular_tool != MolecularTool::select ||
                square_lattice_dragging || wall_tool_active ||
                wall_dragging))
        {
            if (square_lattice_dragging)
            {
                square_lattice_dragging = false;
                square_lattice_for_scenario = false;
                ReleaseCapture();
            }
            if (wall_dragging)
            {
                wall_dragging = false;
                ReleaseCapture();
            }
            wall_tool_active = false;
            set_molecule_tool(window, false);
            return 0;
        }
        if (w_param == '2' && (l_param & (1L << 30)) == 0)
        {
            if (!simulation_engine.config().two_dimensional)
            {
                toggle_dimension_mode(window);
            }
            return 0;
        }
        if (w_param == '3' && (l_param & (1L << 30)) == 0)
        {
            if (simulation_engine.config().two_dimensional)
            {
                toggle_dimension_mode(window);
            }
            return 0;
        }
        if (w_param == 'C' && (l_param & (1L << 30)) == 0)
        {
            set_molecule_tool(window, !molecule_tool_active);
            return 0;
        }
        if (w_param == VK_SPACE && (l_param & (1L << 30)) == 0)
        {
            set_simulation_running(window, !simulation_running);
            return 0;
        }
        if (w_param == 'B' && (l_param & (1L << 30)) == 0)
        {
            set_bond_tool(window, !bond_tool_active);
            return 0;
        }
        if (w_param == VK_OEM_4 && (l_param & (1L << 30)) == 0)
        {
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                adjust_target_temperature(window, -0.05);
            }
            else
            {
                adjust_selected_charge(window, -0.5);
            }
            return 0;
        }
        if (w_param == VK_OEM_6 && (l_param & (1L << 30)) == 0)
        {
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                adjust_target_temperature(window, 0.05);
            }
            else
            {
                adjust_selected_charge(window, 0.5);
            }
            return 0;
        }
        if ((l_param & (1L << 30)) == 0)
        {
            auto& config = simulation_engine.config();
            bool controls_changed = true;
            switch (w_param)
            {
            case 'T':
                switch (config.thermostat)
                {
                case simulation::Thermostat::none:
                    config.thermostat = simulation::Thermostat::andersen;
                    break;
                case simulation::Thermostat::andersen:
                    config.thermostat = simulation::Thermostat::berendsen;
                    break;
                case simulation::Thermostat::berendsen:
                    config.thermostat =
                        simulation::Thermostat::acceleration_damping;
                    break;
                case simulation::Thermostat::acceleration_damping:
                    config.thermostat = simulation::Thermostat::none;
                    break;
                }
                break;
            case 'I':
                config.integrator =
                    config.integrator == simulation::Integrator::velocity_verlet
                    ? simulation::Integrator::kick_drift_kick
                    : simulation::Integrator::velocity_verlet;
                break;
            case 'O':
                config.coulomb_solver =
                    config.coulomb_solver == simulation::CoulombSolver::direct
                    ? simulation::CoulombSolver::barnes_hut
                    : simulation::CoulombSolver::direct;
                break;
            case 'P':
                switch (config.boundary_mode)
                {
                case simulation::BoundaryMode::none:
                    config.boundary_mode = simulation::BoundaryMode::soft;
                    break;
                case simulation::BoundaryMode::soft:
                    config.boundary_mode =
                        simulation::BoundaryMode::reflective;
                    break;
                case simulation::BoundaryMode::reflective:
                    config.boundary_mode = simulation::BoundaryMode::none;
                    break;
                }
                break;
            case 'G':
                config.gravity_z = config.gravity_z == 0.0 ? -1.0 : 0.0;
                break;
            default:
                controls_changed = false;
                break;
            }
            if (controls_changed)
            {
                simulation_engine.invalidate_forces();
                invalidate_simulation_ui(window, true);
                return 0;
            }
        }

        switch (w_param)
        {
        case 'W':
        case 'A':
        case 'S':
        case 'D':
        case 'Q':
        case 'E':
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
            start_camera_motion(window);
            break;
        case VK_HOME:
            reset_view(window);
            break;
        default:
            return DefWindowProcW(window, message, w_param, l_param);
        }
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        InvalidateRect(window, &area, FALSE);
        return 0;
    }

    if (message == WM_MOUSEMOVE)
    {
        const POINT position{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        if (time_slider_dragging)
        {
            set_time_flow_from_pointer(window, position);
            return 0;
        }
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        const bool over_scene = PtInRect(&area, position) != FALSE;
        const int old_hover = hovered_ui_target(
            client_area, workspace_pointer_position);
        const int new_hover = hovered_ui_target(client_area, position);
        const bool scene_changed = position.x != pointer_position.x ||
            position.y != pointer_position.y || over_scene != pointer_over_scene;
        workspace_pointer_position = position;
        pointer_position = position;
        pointer_over_scene = over_scene;
        if (old_hover != new_hover)
        {
            const RECT toolbar = toolbar_area(client_area);
            const RECT rail{client_area.left,
                client_area.top + toolbar_height,
                client_area.left + workspace_rail_width,
                client_area.bottom};
            InvalidateRect(window, &toolbar, FALSE);
            InvalidateRect(window, &rail, FALSE);
            if (tools_panel_visible)
            {
                const RECT panel = tools_panel_area(client_area);
                InvalidateRect(window, &panel, FALSE);
            }
        }

        if (orbit_dragging)
        {
            camera_state.yaw -= (position.x - drag_last_position.x) * 0.008;
            camera_state.pitch = (std::clamp)(
                camera_state.pitch + (position.y - drag_last_position.y) * 0.008,
                -0.08, 1.50);
            drag_last_position = position;
            InvalidateRect(window, &area, FALSE);
        }
        else if (pan_dragging)
        {
            const double scale = camera_state.distance * 0.003;
            if (simulation_engine.config().two_dimensional)
            {
                camera_state.target.x -=
                    (position.x - drag_last_position.x) * scale;
                camera_state.target.y +=
                    (position.y - drag_last_position.y) * scale;
            }
            else
            {
                camera_state.target.x -=
                    (position.x - drag_last_position.x) *
                    std::cos(camera_state.yaw) * scale;
                camera_state.target.y -=
                    (position.x - drag_last_position.x) *
                    std::sin(camera_state.yaw) * scale;
                camera_state.target.z +=
                    (position.y - drag_last_position.y) * scale;
            }
            drag_last_position = position;
            InvalidateRect(window, &area, FALSE);
        }
        else if (molecule_dragging)
        {
            InvalidateRect(window, &area, FALSE);
        }
        else if (square_lattice_dragging)
        {
            Vec3 world_position{};
            if (screen_to_plane(client_area, position,
                    square_lattice_start.z, world_position))
            {
                square_lattice_end = world_position;
            }
            InvalidateRect(window, &area, FALSE);
        }
        else if (wall_dragging)
        {
            Vec3 world_position{};
            if (screen_to_plane(client_area, position, wall_start.z,
                    world_position))
            {
                wall_end = Vec3{
                    std::round(world_position.x),
                    std::round(world_position.y),
                    wall_start.z};
            }
            InvalidateRect(window, &area, FALSE);
        }

        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);

        if (molecule_tool_active && over_scene)
        {
            draw_preview_cursor(window, position);
        }
        if (scene_changed && molecule_tool_active)
        {
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_MOUSELEAVE)
    {
        pointer_over_scene = false;
        workspace_pointer_position = POINT{-1, -1};
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT toolbar = toolbar_area(client_area);
        const RECT rail{client_area.left,
            client_area.top + toolbar_height,
            client_area.left + workspace_rail_width,
            client_area.bottom};
        InvalidateRect(window, &toolbar, FALSE);
        InvalidateRect(window, &rail, FALSE);
        if (molecule_tool_active)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            const RECT area = scene_area(client_area);
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_RBUTTONDOWN && pointer_over_scene &&
        !simulation_engine.config().two_dimensional)
    {
        orbit_dragging = true;
        drag_last_position = POINT{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        SetCapture(window);
        return 0;
    }

    if (message == WM_LBUTTONUP && molecule_dragging)
    {
        stop_molecule_interaction(window);
        ReleaseCapture();
        return 0;
    }

    if (message == WM_LBUTTONUP && time_slider_dragging)
    {
        time_slider_dragging = false;
        ReleaseCapture();
        return 0;
    }

    if (message == WM_LBUTTONUP && square_lattice_dragging)
    {
        const bool scenario_lattice = square_lattice_for_scenario;
        square_lattice_dragging = false;
        ReleaseCapture();
        bool placed = false;
        if (scenario_lattice)
        {
            const double side = (std::max)(
                std::abs(square_lattice_end.x - square_lattice_start.x),
                std::abs(square_lattice_end.y - square_lattice_start.y));
            if (side >= 0.1)
            {
                ScenarioStep step;
                step.action = ScenarioAction::spawn_lattice;
                step.duration_seconds = pending_lattice_duration;
                step.lattice_count = square_lattice_count;
                step.lattice_start = square_lattice_start;
                step.lattice_end = square_lattice_end;
                scenario_steps.push_back(std::move(step));
                placed = true;
            }
            else
            {
                MessageBoxW(window,
                    L"Нарисуйте квадрат размером не менее 0.1 единицы.",
                    L"Шаг сценария", MB_OK | MB_ICONINFORMATION);
            }
            square_lattice_for_scenario = false;
        }
        else
        {
            placed = place_square_lattice(window);
        }
        molecular_tool = MolecularTool::select;
        if (placed)
        {
            invalidate_simulation_ui(window, true);
        }
        else
        {
            RECT client_area{};
            GetClientRect(window, &client_area);
            const RECT area = scene_area(client_area);
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_LBUTTONUP && wall_dragging)
    {
        wall_dragging = false;
        ReleaseCapture();
        simulation::Wall wall{
            wall_start, wall_end,
            !simulation_engine.config().two_dimensional};
        std::string error;
        if (!simulation_engine.add_wall(wall, error))
        {
            const std::wstring message_text(error.begin(), error.end());
            MessageBoxW(window, message_text.c_str(),
                L"Не удалось создать стену", MB_OK | MB_ICONWARNING);
        }
        else
        {
            invalidate_simulation_ui(window, true);
        }
        return 0;
    }

    if (message == WM_RBUTTONUP && orbit_dragging)
    {
        orbit_dragging = false;
        ReleaseCapture();
        return 0;
    }

    if (message == WM_MBUTTONDOWN && pointer_over_scene)
    {
        pan_dragging = true;
        drag_last_position = POINT{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        SetCapture(window);
        return 0;
    }

    if (message == WM_MBUTTONUP && pan_dragging)
    {
        pan_dragging = false;
        ReleaseCapture();
        return 0;
    }

    if (message == WM_CAPTURECHANGED)
    {
        orbit_dragging = false;
        pan_dragging = false;
        time_slider_dragging = false;
        const bool cancelled_lattice = square_lattice_dragging;
        const bool cancelled_wall = wall_dragging;
        square_lattice_dragging = false;
        wall_dragging = false;
        if (cancelled_lattice)
        {
            square_lattice_for_scenario = false;
            molecular_tool = MolecularTool::select;
        }
        if (cancelled_wall)
        {
            wall_tool_active = false;
        }
        if (molecule_dragging)
        {
            stop_molecule_interaction(window);
        }
    }

    if (message == WM_LBUTTONDOWN)
    {
        const POINT position{
            static_cast<short>(LOWORD(l_param)),
            static_cast<short>(HIWORD(l_param))};
        RECT client_area{};
        GetClientRect(window, &client_area);
        if (!separate_interface_windows)
        {
            const WorkspacePage pages[]{
                WorkspacePage::objects,
                WorkspacePage::thermodynamics,
                WorkspacePage::dynamics,
                WorkspacePage::quantum,
                WorkspacePage::settings,
                WorkspacePage::experiments};
            for (const WorkspacePage page : pages)
            {
                const RECT category =
                    workspace_category_area(client_area, page);
                if (PtInRect(&category, position))
                {
                    workspace_page = workspace_page == page
                        ? WorkspacePage::closed : page;
                    tools_panel_visible =
                        workspace_page != WorkspacePage::closed;
                    footer_visible = false;
                    wall_tool_active = false;
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
            }
        }
        const RECT time_slider = time_flow_slider_area(client_area);
        if (toolbar_visible && PtInRect(&time_slider, position))
        {
            time_slider_dragging = true;
            SetCapture(window);
            set_time_flow_from_pointer(window, position);
            return 0;
        }
        const RECT button_area = molecule_tool_area(client_area);
        const RECT simulation_button = simulation_tool_area(client_area);
        const RECT graphics_button = graphics_tool_area(client_area);
        const RECT tools_panel = tools_panel_area(client_area);
        const RECT tools_close{
            tools_panel.right - 34, tools_panel.top,
            tools_panel.right - 4, tools_panel.top + 38};
        if (tools_panel_visible &&
            PtInRect(&tools_close, position))
        {
            tools_panel_visible = false;
            workspace_page = WorkspacePage::closed;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (tools_panel_visible)
        {
            const int row_count =
                workspace_page == WorkspacePage::objects ? 12 :
                workspace_page == WorkspacePage::settings ? 11 :
                workspace_page == WorkspacePage::dynamics ? 7 :
                workspace_page == WorkspacePage::experiments ? 8 : 0;
            for (int row = 0; row < row_count; ++row)
            {
                const RECT tool_button =
                    tools_panel_button_area(client_area, row);
                if (!PtInRect(&tool_button, position))
                {
                    continue;
                }
                if (workspace_page == WorkspacePage::settings)
                {
                    if (row < 5)
                    {
                        apply_visual_style(
                            static_cast<VisualStyle>(row));
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    else if (row == 5)
                    {
                        graphics_quality =
                            graphics_quality == GraphicsQuality::performance
                            ? GraphicsQuality::balanced
                            : graphics_quality == GraphicsQuality::balanced
                            ? GraphicsQuality::quality
                            : GraphicsQuality::performance;
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    else if (row == 6)
                    {
                        grid_visible = !grid_visible;
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    else if (row == 7)
                    {
                        show_chart_window(window);
                    }
                    else if (row == 8)
                    {
                        show_graphics_menu(window, true);
                    }
                    else if (row == 9)
                    {
                        configure_graphics_for_this_pc(window);
                    }
                    else if (row == 10)
                    {
                        restart_application(window);
                    }
                    return 0;
                }
                if (workspace_page == WorkspacePage::dynamics)
                {
                    auto& config = simulation_engine.config();
                    switch (row)
                    {
                    case 0:
                        show_interaction_menu(window);
                        break;
                    case 1:
                        config.thermostat =
                            config.thermostat == simulation::Thermostat::none
                            ? simulation::Thermostat::andersen
                            : config.thermostat == simulation::Thermostat::andersen
                            ? simulation::Thermostat::berendsen
                            : config.thermostat ==
                                simulation::Thermostat::berendsen
                            ? simulation::Thermostat::acceleration_damping
                            : simulation::Thermostat::none;
                        simulation_engine.invalidate_forces();
                        invalidate_simulation_ui(window, true);
                        break;
                    case 2:
                        config.integrator =
                            config.integrator ==
                                simulation::Integrator::velocity_verlet
                            ? simulation::Integrator::kick_drift_kick
                            : simulation::Integrator::velocity_verlet;
                        simulation_engine.invalidate_forces();
                        invalidate_simulation_ui(window, true);
                        break;
                    case 3:
                        config.coulomb_solver =
                            config.coulomb_solver ==
                                simulation::CoulombSolver::barnes_hut
                            ? simulation::CoulombSolver::direct
                            : simulation::CoulombSolver::barnes_hut;
                        simulation_engine.invalidate_forces();
                        invalidate_simulation_ui(window, true);
                        break;
                    case 4:
                        config.boundary_mode =
                            config.boundary_mode ==
                                simulation::BoundaryMode::none
                            ? simulation::BoundaryMode::reflective
                            : simulation::BoundaryMode::none;
                        simulation_engine.invalidate_forces();
                        invalidate_simulation_ui(window, true);
                        break;
                    case 5:
                        config.gravity_z =
                            config.gravity_z == 0.0 ? -1.0 : 0.0;
                        simulation_engine.invalidate_forces();
                        invalidate_simulation_ui(window, true);
                        break;
                    case 6:
                        show_chart_window(window);
                        break;
                    default:
                        break;
                    }
                    return 0;
                }
                if (workspace_page == WorkspacePage::experiments)
                {
                    switch (row)
                    {
                    case 0:
                        show_periodic_table(window);
                        break;
                    case 1:
                        apply_selected_element_to_particles(window);
                        break;
                    case 2:
                        edit_selected_particle_parameters(window);
                        break;
                    case 3:
                        thermalize_experiment_particles(window);
                        break;
                    case 4:
                        reset_experiment_velocities(window);
                        break;
                    case 5:
                        invert_experiment_charges(window);
                        break;
                    case 6:
                        create_gas_sample(window);
                        break;
                    case 7:
                        create_ionic_pair_sample(window);
                        break;
                    default:
                        break;
                    }
                    return 0;
                }
                if (workspace_page != WorkspacePage::objects)
                {
                    return 0;
                }
                switch (row)
                {
                case 0:
                    set_molecule_tool(window, !molecule_tool_active);
                    break;
                case 1:
                    set_bond_tool(window, !bond_tool_active);
                    break;
                case 2:
                    show_interaction_menu(window);
                    break;
                case 3:
                    toggle_selected_particles_fixed(window);
                    break;
                case 4:
                    if (molecular_tool != MolecularTool::multi_select &&
                        selected_particles.empty() &&
                        selected_molecule_id > 0 &&
                        selected_molecule_id <=
                            simulation_engine.particles().size())
                    {
                        selected_particles.push_back(
                            static_cast<std::size_t>(
                                selected_molecule_id - 1));
                    }
                    molecular_tool =
                        molecular_tool == MolecularTool::multi_select
                        ? MolecularTool::select
                        : MolecularTool::multi_select;
                    square_lattice_for_scenario = false;
                    molecule_tool_active = false;
                    bond_tool_active = false;
                    square_lattice_dragging = false;
                    invalidate_simulation_ui(window, true);
                    break;
                case 5:
                    configure_square_lattice(window);
                    break;
                case 6:
                    show_scenario_step_menu(window);
                    break;
                case 7:
                    toggle_scenario(window);
                    break;
                case 8:
                    clear_scenario(window);
                    break;
                case 9:
                    wall_tool_active = !wall_tool_active;
                    molecule_tool_active = false;
                    bond_tool_active = false;
                    molecular_tool = MolecularTool::select;
                    square_lattice_dragging = false;
                    square_lattice_for_scenario = false;
                    wall_dragging = false;
                    invalidate_simulation_ui(window, true);
                    break;
                case 10:
                    toggle_dimension_mode(window);
                    break;
                case 11:
                    show_periodic_table(window);
                    break;
                default:
                    break;
                }
                return 0;
            }
        }
        const RECT footer = footer_area(client_area);
        const RECT footer_close{
            footer.right - 34, footer.top,
            footer.right - 4, footer.top + 24};
        if (footer_visible &&
            PtInRect(&footer_close, position))
        {
            footer_visible = false;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (toolbar_visible && PtInRect(&graphics_button, position))
        {
            SetFocus(window);
            show_graphics_menu(window);
            return 0;
        }
        if (toolbar_visible && PtInRect(&simulation_button, position))
        {
            SetFocus(window);
            set_simulation_running(window, !simulation_running);
            return 0;
        }
        if (toolbar_visible && PtInRect(&button_area, position))
        {
            SetFocus(window);
            set_molecule_tool(window, !molecule_tool_active);
            return 0;
        }
        const RECT area = scene_area(client_area);
        if (bond_tool_active && PtInRect(&area, position))
        {
            select_bond_endpoint(window, position);
            return 0;
        }
        if (molecular_tool == MolecularTool::square_lattice &&
            PtInRect(&area, position))
        {
            Vec3 world_position{};
            const double placement_z =
                simulation_engine.config().two_dimensional
                ? 0.0 : molecule_placement_height;
            if (screen_to_plane(client_area, position,
                    placement_z, world_position))
            {
                square_lattice_start = world_position;
                square_lattice_end = world_position;
                square_lattice_dragging = true;
                SetCapture(window);
                InvalidateRect(window, &area, FALSE);
            }
            return 0;
        }
        if (wall_tool_active && PtInRect(&area, position))
        {
            const bool two_dimensional =
                simulation_engine.config().two_dimensional;
            const double wall_z = two_dimensional
                ? 0.0 : std::round(camera_state.target.z);
            Vec3 world_position{};
            if (screen_to_plane(client_area, position, wall_z,
                    world_position))
            {
                wall_start = Vec3{
                    std::round(world_position.x),
                    std::round(world_position.y), wall_z};
                wall_end = wall_start;
                wall_dragging = true;
                SetCapture(window);
                InvalidateRect(window, &area, FALSE);
            }
            return 0;
        }
        if (molecular_tool == MolecularTool::multi_select &&
            PtInRect(&area, position))
        {
            select_or_toggle_particle(window,
                pick_molecule(client_area, position));
            return 0;
        }
        if (molecule_tool_active && PtInRect(&area, position))
        {
            place_molecule(window, position);
            invalidate_simulation_ui(window, false);
            return 0;
        }
        if (PtInRect(&area, position))
        {
            if (begin_molecule_interaction(window, client_area, position))
            {
                if (molecule_dragging)
                {
                    SetCapture(window);
                }
                invalidate_simulation_ui(window, false);
                return 0;
            }
            selected_molecule_id = 0;
            invalidate_simulation_ui(window, false);
        }
    }

    if (message == WM_SETCURSOR && LOWORD(l_param) == HTCLIENT)
    {
        if (molecule_tool_active)
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
        if (orbit_dragging || pan_dragging)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32516)));
            return TRUE;
        }
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
        if (PtInRect(&area, cursor))
        {
            const double wheel_steps =
                static_cast<double>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA;
            if (molecule_tool_active &&
                (LOWORD(w_param) & MK_SHIFT) != 0)
            {
                molecule_placement_height = (std::clamp)(
                    molecule_placement_height + wheel_steps * 0.25,
                    molecule_radius, 50.0);
                InvalidateRect(window, &area, FALSE);
                const RECT button_area = molecule_tool_area(client_area);
                InvalidateRect(window, &button_area, FALSE);
                return 0;
            }
            camera_state.distance = (std::clamp)(
                camera_state.distance * std::pow(camera_zoom_factor, -wheel_steps),
                camera_minimum_distance, camera_maximum_distance);
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_TIMER && w_param == camera_timer_id)
    {
        update_camera_motion(window);
        return 0;
    }

    if (message == WM_TIMER && w_param == simulation_timer_id)
    {
        update_simulation(window);
        return 0;
    }

    if (message == WM_TIMER && w_param == telemetry_timer_id)
    {
        const bool memory_available = update_process_memory_usage();
        if (ram_budget_bytes != 0 && simulation_running &&
            (!memory_available || process_private_bytes >= ram_budget_bytes))
        {
            set_simulation_running(window, false);
            MessageBoxW(window, memory_available
                    ? L"Simulation paused because the configured RAM budget "
                      L"was reached. Increase the budget in OPT or remove "
                      L"molecules."
                    : L"Simulation paused because memory usage could not be "
                      L"measured while a RAM budget is active.",
                L"Memory budget", memory_available
                    ? MB_OK | MB_ICONWARNING
                    : MB_OK | MB_ICONERROR);
        }
        const ULONGLONG now = GetTickCount64();
        if (telemetry.simulation_sample_started_at == 0)
        {
            telemetry.simulation_sample_started_at = now;
            telemetry.last_simulation_step_sample =
                simulation_engine.metrics().steps;
        }
        else
        {
            const ULONGLONG elapsed = now -
                telemetry.simulation_sample_started_at;
            if (elapsed >= 500)
            {
                const std::uint64_t steps =
                    simulation_engine.metrics().steps -
                    telemetry.last_simulation_step_sample;
                telemetry.simulation_steps_per_second =
                    static_cast<double>(steps) * 1000.0 /
                    static_cast<double>(elapsed);
                telemetry.last_simulation_step_sample =
                    simulation_engine.metrics().steps;
                telemetry.simulation_sample_started_at = now;
            }
        }
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT footer = footer_area(client_area);
        if (footer_visible)
        {
            InvalidateRect(window, &footer, FALSE);
        }
        return 0;
    }

    if (message == WM_KILLFOCUS)
    {
        stop_camera_motion(window);
        orbit_dragging = false;
        pan_dragging = false;
        if (molecule_dragging)
        {
            stop_molecule_interaction(window);
        }
        ReleaseCapture();
    }

    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        limits->ptMinTrackSize.x = 1100;
        limits->ptMinTrackSize.y = 540;
        return 0;
    }

    if (message == WM_DESTROY)
    {
        separate_interface_windows = false;
        if (detached_settings_window != nullptr)
        {
            DestroyWindow(detached_settings_window);
            detached_settings_window = nullptr;
        }
        if (detached_interface_window != nullptr)
        {
            DestroyWindow(detached_interface_window);
            detached_interface_window = nullptr;
        }
        KillTimer(window, camera_timer_id);
        KillTimer(window, telemetry_timer_id);
        KillTimer(window, simulation_timer_id);
        if (open_gl_context != nullptr)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(open_gl_context);
            open_gl_context = nullptr;
        }
        destroy_overlay_fonts();
        if (window_device_context != nullptr)
        {
            ReleaseDC(window, window_device_context);
            window_device_context = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command)
{
    apply_visual_style(visual_style);
    QueryPerformanceFrequency(&performance_frequency);
    telemetry.started_at = GetTickCount64();

    WNDCLASSW window_class{};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = window_class_name;

    if (RegisterClassW(&window_class) == 0)
    {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        window_class_name,
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
        MessageBoxW(nullptr,
            L"Unable to initialize the 3D graphics context. OpenGL is required.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return 1;
    }

    main_window = window;
    ShowWindow(window, show_command);
    SetTimer(window, telemetry_timer_id, 100, nullptr);
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
