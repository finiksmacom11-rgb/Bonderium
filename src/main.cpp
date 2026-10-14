#include <windows.h>
#include <GL/gl.h>
#include <psapi.h>
#include <shellapi.h>

#include "simulation.h"
#include "updater.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>

namespace
{
constexpr wchar_t window_title[] = L"Bonderium — 3D Molecular Space";
constexpr wchar_t window_class_name[] = L"BonderiumWindow";

constexpr int toolbar_height = 62;
constexpr int footer_height = 116;
constexpr int molecule_tool_left = 258;
constexpr int molecule_tool_width = 148;
constexpr int molecule_tool_top = 14;
constexpr int molecule_tool_height = 34;
constexpr int simulation_tool_left = 420;
constexpr int simulation_tool_width = 126;
constexpr int simulation_tool_top = 14;
constexpr int simulation_tool_height = 34;
constexpr int graphics_tool_left = 724;
constexpr int graphics_tool_width = 60;
constexpr int graphics_tool_top = 14;
constexpr int graphics_tool_height = 34;
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

constexpr double camera_default_distance = 28.0;
constexpr double camera_minimum_distance = 3.0;
constexpr double camera_maximum_distance = 240.0;
constexpr double camera_zoom_factor = 1.15;
constexpr double camera_move_speed = 12.0;
constexpr double camera_ease_seconds = 0.12;
constexpr double camera_stop_threshold = 0.015;
constexpr double molecule_radius = 0.42;
constexpr double molecule_sigma = 0.75;
constexpr double molecule_epsilon = 0.1;
constexpr double spring_drag_stiffness = 30.0;
constexpr double directional_force_scale = 18.0;
constexpr double maximum_interaction_force = 200.0;
constexpr double field_of_view = 45.0;
constexpr double pi = 3.14159265358979323846;

constexpr COLORREF canvas_color = RGB(10, 14, 22);
constexpr COLORREF toolbar_color = RGB(15, 20, 30);
constexpr COLORREF footer_color = RGB(13, 18, 27);
constexpr COLORREF border_color = RGB(37, 49, 66);
constexpr COLORREF primary_text_color = RGB(228, 236, 247);
constexpr COLORREF muted_text_color = RGB(128, 144, 164);
constexpr COLORREF x_axis_color = RGB(69, 214, 201);
constexpr COLORREF y_axis_color = RGB(160, 132, 255);
constexpr COLORREF z_axis_color = RGB(255, 174, 94);

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
    menu_ram_custom
};

enum class InteractionTool
{
    select,
    spring_drag,
    directional_force
};

enum InteractionMenuCommand : UINT
{
    menu_interaction_select = 101,
    menu_interaction_spring_drag,
    menu_interaction_directional_force
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
bool fullscreen = false;
WINDOWPLACEMENT windowed_placement{};
LONG_PTR windowed_style = 0;
LONG_PTR windowed_extended_style = 0;
bool molecule_tool_active = false;
bool bond_tool_active = false;
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

bool rectangles_intersect(const RECT& first, const RECT& second)
{
            return first.left < second.right && first.right > second.left &&
                first.top < second.bottom && first.bottom > second.top;
}

RECT molecule_tool_area(const RECT& client_area)
{
    return RECT{
        client_area.left + molecule_tool_left,
        client_area.top + molecule_tool_top,
        client_area.left + molecule_tool_left + molecule_tool_width,
        client_area.top + molecule_tool_top + molecule_tool_height};
}

RECT simulation_tool_area(const RECT& client_area)
{
        return RECT{
            client_area.left + simulation_tool_left,
            client_area.top + simulation_tool_top,
            client_area.left + simulation_tool_left + simulation_tool_width,
            client_area.top + simulation_tool_top + simulation_tool_height};
}

RECT graphics_tool_area(const RECT& client_area)
{
    return RECT{
        client_area.left + graphics_tool_left,
        client_area.top + graphics_tool_top,
        client_area.left + graphics_tool_left + graphics_tool_width,
        client_area.top + graphics_tool_top + graphics_tool_height};
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
        const RECT footer{
            client_area.left, client_area.bottom - footer_height,
            client_area.right, client_area.bottom};
        InvalidateRect(window, &scene, FALSE);
        InvalidateRect(window, &footer, FALSE);
        if (toolbar)
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
    const Vec3 right = normalized(cross(forward, Vec3{0.0, 0.0, 1.0}));
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
        glColor4ub(major ? 36 : 25, major ? 49 : 34, major ? 65 : 47, 255);
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
        glColor4ub(major ? 36 : 25, major ? 49 : 34, major ? 65 : 47, 255);
        glVertex3d(minimum_x, y, 0.0);
        glVertex3d(maximum_x, y, 0.0);
    }
    glEnd();

    glLineWidth(2.0F);
    glBegin(GL_LINES);
    glColor3ub(69, 214, 201);
    glVertex3d(minimum_x, 0.0, 0.025);
    glVertex3d(maximum_x, 0.0, 0.025);
    glColor3ub(160, 132, 255);
    glVertex3d(0.0, minimum_y, 0.025);
    glVertex3d(0.0, maximum_y, 0.025);
    glColor3ub(255, 174, 94);
    glVertex3d(0.0, 0.0, 0.025);
    glVertex3d(0.0, 0.0, extent);
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
    glColor3ub(config.boundary_mode == simulation::BoundaryMode::soft
            ? 255 : 160,
        config.boundary_mode == simulation::BoundaryMode::soft
            ? 174 : 132,
        config.boundary_mode == simulation::BoundaryMode::soft
            ? 94 : 255);
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

void draw_sphere(const Vec3& center, double radius, bool preview = false,
    bool selected = false, double charge = 0.0)
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
        glColor4ub(69, 214, 201, 90);
    }
    else
    {
        glEnable(GL_LIGHTING);
        if (selected)
        {
            glColor3ub(116, 204, 188);
        }
        else
        {
            glColor3ub(charge > 0.0 ? 225 : charge < 0.0 ? 151 : 69,
                charge < 0.0 ? 132 : 154,
                charge > 0.0 ? 110 : charge < 0.0 ? 232 : 160);
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
        draw_sphere(position, molecule_radius, false,
            static_cast<std::uint64_t>(index + 1) == selected_molecule_id,
            particles[index].charge);
    }

    Vec3 preview_position{};
    if (molecule_tool_active && pointer_over_scene &&
        screen_to_plane(client_area, pointer_position,
            molecule_placement_height, preview_position))
    {
        draw_sphere(
            Vec3{preview_position.x, preview_position.y, molecule_placement_height},
            molecule_radius, true);
    }
}

void draw_bonds()
{
    glDisable(GL_LIGHTING);
    glLineWidth(2.0F);
    glColor3ub(255, 174, 94);
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
    glColor3ub(255, 174, 94);
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
    glClearColor(10.0F / 255.0F, 14.0F / 255.0F, 22.0F / 255.0F, 1.0F);
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
    draw_bonds();
    draw_molecules(client_area);
    draw_interaction_force();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

void draw_toolbar(HDC device_context, const RECT& client_area, HFONT title_font,
    HFONT small_font, HFONT button_font, HFONT status_font)
{
    RECT toolbar{client_area.left, client_area.top, client_area.right,
        client_area.top + toolbar_height};
    fill_rectangle(device_context, toolbar, toolbar_color);
    draw_line(device_context, client_area.left, toolbar.bottom - 1,
        client_area.right, toolbar.bottom - 1, border_color);

    const int brand_center_y = toolbar.top + toolbar_height / 2;
    draw_filled_circle(device_context, 29, brand_center_y, 8,
        RGB(21, 57, 65), x_axis_color);
    RECT title_area{49, toolbar.top + 9, 225, toolbar.top + 35};
    draw_text(device_context, L"BONDERIUM", title_area,
        primary_text_color, title_font);
    RECT subtitle_area{50, toolbar.top + 34, 245, toolbar.top + 51};
    draw_text(device_context, L"3D MOLECULAR SPACE", subtitle_area,
        muted_text_color, small_font);

    const RECT molecule_button = molecule_tool_area(client_area);
    HPEN button_pen = CreatePen(PS_SOLID, 1,
        molecule_tool_active ? RGB(69, 142, 139) : border_color);
    HBRUSH button_brush = CreateSolidBrush(
        molecule_tool_active ? RGB(26, 55, 61) : RGB(20, 27, 39));
    HPEN previous_pen = static_cast<HPEN>(SelectObject(device_context, button_pen));
    HBRUSH previous_brush =
        static_cast<HBRUSH>(SelectObject(device_context, button_brush));
    RoundRect(device_context, molecule_button.left, molecule_button.top,
        molecule_button.right, molecule_button.bottom, 10, 10);
    SelectObject(device_context, previous_brush);
    SelectObject(device_context, previous_pen);
    DeleteObject(button_brush);
    DeleteObject(button_pen);
    wchar_t molecule_tool_label[40]{};
    if (molecule_tool_active)
    {
        std::swprintf(molecule_tool_label,
            sizeof(molecule_tool_label) / sizeof(molecule_tool_label[0]),
            L"MOLECULE  Z %.1f", molecule_placement_height);
    }
    else
    {
        std::swprintf(molecule_tool_label,
            sizeof(molecule_tool_label) / sizeof(molecule_tool_label[0]),
            L"+  Molecule");
    }
    RECT button_text{molecule_button.left + 8, molecule_button.top + 1,
        molecule_button.right - 8, molecule_button.bottom - 1};
    draw_text(device_context, molecule_tool_label, button_text,
        molecule_tool_active ? x_axis_color : primary_text_color,
        button_font, DT_CENTER);

    const RECT simulation_button = simulation_tool_area(client_area);
    const COLORREF simulation_button_fill = simulation_running
        ? RGB(23, 63, 57)
        : RGB(20, 27, 39);
    const COLORREF simulation_button_border = simulation_running
        ? RGB(58, 158, 126)
        : border_color;
    HPEN simulation_pen = CreatePen(PS_SOLID, 1, simulation_button_border);
    HBRUSH simulation_brush = CreateSolidBrush(simulation_button_fill);
    HPEN previous_simulation_pen =
        static_cast<HPEN>(SelectObject(device_context, simulation_pen));
    HBRUSH previous_simulation_brush =
        static_cast<HBRUSH>(SelectObject(device_context, simulation_brush));
    RoundRect(device_context, simulation_button.left, simulation_button.top,
        simulation_button.right, simulation_button.bottom, 10, 10);
    SelectObject(device_context, previous_simulation_brush);
    SelectObject(device_context, previous_simulation_pen);
    DeleteObject(simulation_brush);
    DeleteObject(simulation_pen);
    RECT simulation_label{
        simulation_button.left + 6, simulation_button.top + 1,
        simulation_button.right - 6, simulation_button.bottom - 1};
    draw_text(device_context,
        simulation_running ? L"PAUSE EXP" : L"START EXP",
        simulation_label,
        simulation_running ? x_axis_color : primary_text_color,
        button_font, DT_CENTER);

    const auto& config = simulation_engine.config();
    const wchar_t* thermostat_label = L"OFF";
    switch (config.thermostat)
    {
    case simulation::Thermostat::andersen:
        thermostat_label = L"AND";
        break;
    case simulation::Thermostat::berendsen:
        thermostat_label = L"BER";
        break;
    case simulation::Thermostat::acceleration_damping:
        thermostat_label = L"DAMP";
        break;
    case simulation::Thermostat::none:
        break;
    }
    wchar_t model_label[48]{};
    std::swprintf(model_label, sizeof(model_label) / sizeof(model_label[0]),
        L"%ls  %ls  %ls  %.2f",
        config.integrator == simulation::Integrator::velocity_verlet
            ? L"VV" : L"KDK",
        thermostat_label,
        config.coulomb_solver == simulation::CoulombSolver::direct
            ? L"COUL" : L"TREE",
        config.target_temperature);
    draw_text(device_context, model_label,
        RECT{client_area.left + 554, toolbar.top + 16,
            client_area.left + 648, toolbar.top + 46},
        muted_text_color, small_font, DT_CENTER);

    const RECT graphics_button = graphics_tool_area(client_area);
    HPEN graphics_pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH graphics_brush = CreateSolidBrush(RGB(20, 27, 39));
    HPEN previous_graphics_pen =
        static_cast<HPEN>(SelectObject(device_context, graphics_pen));
    HBRUSH previous_graphics_brush =
        static_cast<HBRUSH>(SelectObject(device_context, graphics_brush));
    RoundRect(device_context, graphics_button.left, graphics_button.top,
        graphics_button.right, graphics_button.bottom, 10, 10);
    SelectObject(device_context, previous_graphics_brush);
    SelectObject(device_context, previous_graphics_pen);
    DeleteObject(graphics_brush);
    DeleteObject(graphics_pen);
    draw_text(device_context, L"OPT", graphics_button, primary_text_color,
        button_font, DT_CENTER);

    const RECT interaction_button = interaction_tool_area(client_area);
    HPEN interaction_pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH interaction_brush = CreateSolidBrush(
        molecule_dragging ? RGB(26, 55, 61) : RGB(20, 27, 39));
    HPEN previous_interaction_pen =
        static_cast<HPEN>(SelectObject(device_context, interaction_pen));
    HBRUSH previous_interaction_brush =
        static_cast<HBRUSH>(SelectObject(device_context, interaction_brush));
    RoundRect(device_context, interaction_button.left, interaction_button.top,
        interaction_button.right, interaction_button.bottom, 10, 10);
    SelectObject(device_context, previous_interaction_brush);
    SelectObject(device_context, previous_interaction_pen);
    DeleteObject(interaction_brush);
    DeleteObject(interaction_pen);
    const wchar_t* interaction_label =
        interaction_tool == InteractionTool::select ? L"SELECT" :
        interaction_tool == InteractionTool::spring_drag ? L"SPRING" :
        L"FORCE";
    draw_text(device_context, interaction_label, interaction_button,
        molecule_dragging ? x_axis_color : primary_text_color,
        button_font, DT_CENTER);

    const RECT auto_button = auto_config_tool_area(client_area);
    HPEN auto_pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH auto_brush = CreateSolidBrush(RGB(20, 27, 39));
    HPEN previous_auto_pen =
        static_cast<HPEN>(SelectObject(device_context, auto_pen));
    HBRUSH previous_auto_brush =
        static_cast<HBRUSH>(SelectObject(device_context, auto_brush));
    RoundRect(device_context, auto_button.left, auto_button.top,
        auto_button.right, auto_button.bottom, 10, 10);
    SelectObject(device_context, previous_auto_brush);
    SelectObject(device_context, previous_auto_pen);
    DeleteObject(auto_brush);
    DeleteObject(auto_pen);
    draw_text(device_context, L"AUTO", auto_button, primary_text_color,
        button_font, DT_CENTER);

    const RECT restart_button = restart_tool_area(client_area);
    HPEN restart_pen = CreatePen(PS_SOLID, 1, border_color);
    HBRUSH restart_brush = CreateSolidBrush(RGB(20, 27, 39));
    HPEN previous_restart_pen =
        static_cast<HPEN>(SelectObject(device_context, restart_pen));
    HBRUSH previous_restart_brush =
        static_cast<HBRUSH>(SelectObject(device_context, restart_brush));
    RoundRect(device_context, restart_button.left, restart_button.top,
        restart_button.right, restart_button.bottom, 10, 10);
    SelectObject(device_context, previous_restart_brush);
    SelectObject(device_context, previous_restart_pen);
    DeleteObject(restart_brush);
    DeleteObject(restart_pen);
    draw_text(device_context, L"RESTART", restart_button,
        primary_text_color, small_font, DT_CENTER);

    draw_text(device_context, L"X", RECT{654, toolbar.top + 16, 670, toolbar.top + 46},
        x_axis_color, status_font, DT_CENTER);
    draw_text(device_context, L"Y", RECT{678, toolbar.top + 16, 694, toolbar.top + 46},
        y_axis_color, status_font, DT_CENTER);
    draw_text(device_context, L"Z", RECT{702, toolbar.top + 16, 718, toolbar.top + 46},
        z_axis_color, status_font, DT_CENTER);

    if (scene_width(client_area) > 1380)
    {
        wchar_t camera_label[48]{};
        std::swprintf(camera_label,
            sizeof(camera_label) / sizeof(camera_label[0]),
            L"CAMERA %.1f u", camera_state.distance);
        RECT camera_area{client_area.right - 330, toolbar.top + 16,
            client_area.right - 220, toolbar.top + 46};
        draw_text(device_context, camera_label, camera_area, muted_text_color,
            small_font, DT_RIGHT);

        wchar_t molecule_count[32]{};
        std::swprintf(molecule_count,
            sizeof(molecule_count) / sizeof(molecule_count[0]),
            L"%llu",             static_cast<unsigned long long>(
                simulation_engine.particles().size()));
        RECT count_area{client_area.right - 196, toolbar.top + 16,
            client_area.right - 164, toolbar.top + 46};
        draw_text(device_context, molecule_count, count_area,
            primary_text_color, status_font, DT_RIGHT);
    }
    draw_filled_circle(device_context, client_area.right - 42,
        brand_center_y, 5, RGB(22, 53, 64), x_axis_color);
}

void draw_footer(HDC device_context, const RECT& client_area, HFONT small_font,
    HFONT status_font)
{
    RECT footer{client_area.left, client_area.bottom - footer_height,
        client_area.right, client_area.bottom};
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
        : L"WASD/QE MOVE   RMB ORBIT   MMB PAN   WHEEL ZOOM   SPACE SIM";
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
            button_font, status_font);
    }
}

void draw_footer_overlay(HDC device_context, const RECT& client_area)
{
    if (small_font != nullptr && status_font != nullptr)
    {
        draw_footer(device_context, client_area, small_font, status_font);
    }
}

void draw_overlay(HDC device_context, const RECT& client_area)
{
    draw_toolbar_overlay(device_context, client_area);
    draw_footer_overlay(device_context, client_area);
}

bool set_window_long_checked(HWND window, int index, LONG_PTR value);

void toggle_fullscreen(HWND window)
{
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

bool ram_budget_allows_molecule(HWND window)
{
    if (ram_budget_bytes == 0)
    {
        return true;
    }
    if (!update_process_memory_usage())
    {
        MessageBoxW(window,
            L"Unable to measure memory usage. The molecule was not added "
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
    const std::uint64_t projected_bytes = process_private_bytes +
        estimated_particle_bytes +
        estimated_neighbors * estimated_neighbor_pair_bytes;
    if (projected_bytes <= ram_budget_bytes)
    {
        return true;
    }

    MessageBoxW(window,
        L"Adding this molecule would exceed the configured RAM budget. "
        L"Increase the budget in OPT or remove molecules.",
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

void show_graphics_menu(HWND window)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        MessageBoxW(window, L"Unable to open graphics settings.",
            L"Bonderium", MB_OK | MB_ICONERROR);
        return;
    }

    UINT fullscreen_flags = MF_STRING;
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
    const RECT button_area = molecule_tool_area(client_area);
    InvalidateRect(window, &button_area, FALSE);
    const RECT simulation_button = simulation_tool_area(client_area);
    InvalidateRect(window, &simulation_button, FALSE);
    const RECT footer{
        client_area.left, client_area.bottom - footer_height,
        client_area.right, client_area.bottom};
    InvalidateRect(window, &footer, FALSE);
    if (over_scene)
    {
        InvalidateRect(window, &area, FALSE);
    }
}

void set_bond_tool(HWND window, bool active)
{
    bond_tool_active = active;
    pending_bond_molecule_id = 0;
    molecule_tool_active = false;
    pointer_over_scene = false;
    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    invalidate_simulation_ui(window, true);
}

void stop_molecule_interaction(HWND window);

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

void update_simulation(HWND window)
{
    const ULONGLONG now = GetTickCount64();
    const double elapsed = (std::min)(0.05,
        static_cast<double>(now - last_simulation_tick) / 1000.0);
    last_simulation_tick = now;
    const double time_step = simulation_engine.config().time_step;
    simulation_time_accumulator = (std::min)(
        simulation_time_accumulator + elapsed,
        time_step * maximum_catch_up_steps);

    unsigned int completed_steps = 0;
    std::string error;
    if (molecule_dragging && !update_interaction_force(window))
    {
        set_simulation_running(window, false);
        return;
    }
    while (simulation_time_accumulator >= time_step &&
        completed_steps < maximum_catch_up_steps)
    {
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
    const int vertical = static_cast<int>(is_key_down('E')) -
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

void place_molecule(HWND window, POINT position)
{
    if (!ram_budget_allows_molecule(window))
    {
        return;
    }
    RECT client_area{};
    GetClientRect(window, &client_area);
    Vec3 world_position{};
    if (!screen_to_plane(client_area, position,
            molecule_placement_height, world_position))
    {
        return;
    }
    const std::size_t particle_index =
        simulation_engine.add_particle(world_position);
    if (!simulation_engine.set_particle_properties(particle_index, 1.0, 0.0,
            molecule_sigma, molecule_epsilon, false))
    {
        MessageBoxW(window, L"Unable to initialize molecule physics properties.",
            L"Simulation error", MB_OK | MB_ICONERROR);
        return;
    }
    selected_molecule_id =
        static_cast<std::uint64_t>(particle_index + 1);
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
        return false;
    }

    selected_molecule_id = molecule_id;
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
        else
        {
            const RECT toolbar{
                client_area.left, client_area.top,
                client_area.right, client_area.top + toolbar_height};
            const RECT footer{
                client_area.left, client_area.bottom - footer_height,
                client_area.right, client_area.bottom};
            if (rectangles_intersect(paint.rcPaint, toolbar))
            {
                draw_toolbar_overlay(paint_context, client_area);
            }
            if (rectangles_intersect(paint.rcPaint, footer))
            {
                draw_footer_overlay(paint_context, client_area);
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
            toggle_fullscreen(window);
            return 0;
        }
        if (w_param == VK_ESCAPE && (molecule_tool_active || bond_tool_active))
        {
            set_molecule_tool(window, false);
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
        RECT client_area{};
        GetClientRect(window, &client_area);
        const RECT area = scene_area(client_area);
        const bool over_scene = PtInRect(&area, position) != FALSE;
        const bool changed = position.x != pointer_position.x ||
            position.y != pointer_position.y || over_scene != pointer_over_scene;
        pointer_position = position;
        pointer_over_scene = over_scene;

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
            camera_state.target.x -= (position.x - drag_last_position.x) *
                std::cos(camera_state.yaw) * scale;
            camera_state.target.y -= (position.x - drag_last_position.x) *
                std::sin(camera_state.yaw) * scale;
            camera_state.target.z += (position.y - drag_last_position.y) * scale;
            drag_last_position = position;
            InvalidateRect(window, &area, FALSE);
        }
        else if (molecule_dragging)
        {
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
        if (changed && molecule_tool_active)
        {
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_MOUSELEAVE)
    {
        pointer_over_scene = false;
        if (molecule_tool_active)
        {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            RECT client_area{};
            GetClientRect(window, &client_area);
            const RECT area = scene_area(client_area);
            InvalidateRect(window, &area, FALSE);
        }
        return 0;
    }

    if (message == WM_RBUTTONDOWN && pointer_over_scene)
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
        const RECT button_area = molecule_tool_area(client_area);
        const RECT simulation_button = simulation_tool_area(client_area);
        const RECT graphics_button = graphics_tool_area(client_area);
        const RECT interaction_button =
            interaction_tool_area(client_area);
        const RECT auto_button = auto_config_tool_area(client_area);
        const RECT restart_button = restart_tool_area(client_area);
        if (PtInRect(&auto_button, position))
        {
            SetFocus(window);
            configure_graphics_for_this_pc(window);
            return 0;
        }
        if (PtInRect(&restart_button, position))
        {
            SetFocus(window);
            restart_application(window);
            return 0;
        }
        if (PtInRect(&interaction_button, position))
        {
            SetFocus(window);
            show_interaction_menu(window);
            return 0;
        }
        if (PtInRect(&graphics_button, position))
        {
            SetFocus(window);
            show_graphics_menu(window);
            return 0;
        }
        if (PtInRect(&simulation_button, position))
        {
            SetFocus(window);
            set_simulation_running(window, !simulation_running);
            return 0;
        }
        if (PtInRect(&button_area, position))
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
        const RECT footer{client_area.left, client_area.bottom - footer_height,
            client_area.right, client_area.bottom};
        InvalidateRect(window, &footer, FALSE);
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
