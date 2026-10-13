#include <windows.h>
#include <GL/gl.h>

#include "simulation.h"
#include "updater.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace
{
constexpr wchar_t window_title[] = L"Bonderium — 3D Molecular Space";
constexpr wchar_t window_class_name[] = L"BonderiumWindow";

constexpr int toolbar_height = 62;
constexpr int footer_height = 76;
constexpr int molecule_tool_left = 258;
constexpr int molecule_tool_width = 148;
constexpr int molecule_tool_top = 14;
constexpr int molecule_tool_height = 34;
constexpr int simulation_tool_left = 420;
constexpr int simulation_tool_width = 126;
constexpr int simulation_tool_top = 14;
constexpr int simulation_tool_height = 34;
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

struct Molecule
{
    std::uint64_t id;
    Vec3 position;
    double radius;
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
std::vector<Molecule> molecules;
simulation::Engine simulation_engine;
std::uint64_t next_molecule_id = 1;
HDC window_device_context = nullptr;
HGLRC open_gl_context = nullptr;
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
std::uint64_t selected_molecule_id = 0;
Vec3 molecule_drag_offset{};
POINT pointer_position{};
POINT drag_last_position{};

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
            InvalidateRect(window, &molecule_button, FALSE);
            InvalidateRect(window, &simulation_button, FALSE);
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

Molecule* pick_molecule(const RECT& client_area, POINT screen)
{
    const RECT area = scene_area(client_area);
    if (!PtInRect(&area, screen))
    {
        return nullptr;
    }

    Molecule* selected = nullptr;
    double nearest_depth = camera_maximum_distance;
    const double pixels_per_unit =
        static_cast<double>(area.bottom - area.top) /
        (2.0 * std::tan(field_of_view * pi / 360.0));
    for (Molecule& molecule : molecules)
    {
        POINT center{};
        double depth = 0.0;
        if (!world_to_screen(client_area, molecule.position, center, depth))
        {
            continue;
        }
        const double screen_radius = molecule.radius * pixels_per_unit / depth + 5.0;
        const double dx = static_cast<double>(screen.x - center.x);
        const double dy = static_cast<double>(screen.y - center.y);
        if (dx * dx + dy * dy <= screen_radius * screen_radius &&
            depth < nearest_depth)
        {
            nearest_depth = depth;
            selected = &molecule;
        }
    }

    return selected;
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
    const double extent = grid_extent();
    const double minimum_x = camera_state.target.x - extent;
    const double maximum_x = camera_state.target.x + extent;
    const double minimum_y = camera_state.target.y - extent;
    const double maximum_y = camera_state.target.y + extent;
    const int first_x = static_cast<int>(std::floor(minimum_x));
    const int last_x = static_cast<int>(std::ceil(maximum_x));
    const int first_y = static_cast<int>(std::floor(minimum_y));
    const int last_y = static_cast<int>(std::ceil(maximum_y));

    glDisable(GL_LIGHTING);
    glLineWidth(1.0F);
    glBegin(GL_LINES);
    for (int x = first_x; x <= last_x; ++x)
    {
        const bool major = x % 5 == 0;
        glColor4ub(major ? 36 : 25, major ? 49 : 34, major ? 65 : 47, 255);
        glVertex3d(x, minimum_y, 0.0);
        glVertex3d(x, maximum_y, 0.0);
    }
    for (int y = first_y; y <= last_y; ++y)
    {
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
    constexpr int latitude_steps = 12;
    constexpr int longitude_steps = 18;
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
    for (const Molecule& molecule : molecules)
    {
        const Vec3 offset = molecule.position - camera_state.target;
        if (dot(offset, offset) >
            (cull_distance + molecule.radius) * (cull_distance + molecule.radius))
        {
            continue;
        }
        const std::size_t particle_index =
            static_cast<std::size_t>(molecule.id - 1);
        const double charge =
            particle_index < simulation_engine.particles().size()
            ? simulation_engine.particles()[particle_index].charge
            : 0.0;
        draw_sphere(molecule.position, molecule.radius,
            false, molecule.id == selected_molecule_id, charge);
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
    for (const simulation::Bond& bond : simulation_engine.bonds())
    {
        if (bond.first >= molecules.size() || bond.second >= molecules.size())
        {
            continue;
        }
        const Vec3 first = molecules[bond.first].position;
        const Vec3 second = molecules[bond.second].position;
        glVertex3d(first.x, first.y, first.z);
        glVertex3d(second.x, second.y, second.z);
    }
    glEnd();
    glLineWidth(1.0F);
}

void render_scene(const RECT& client_area)
{
    const RECT area = scene_area(client_area);
    const int width = scene_width(client_area);
    const int height = (std::max)(1, static_cast<int>(client_area.bottom));

    glDrawBuffer(GL_FRONT);
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
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    draw_grid();
    draw_simulation_boundaries();
    draw_bonds();
    draw_molecules(client_area);

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glFlush();
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

    draw_text(device_context, L"X", RECT{654, toolbar.top + 16, 670, toolbar.top + 46},
        x_axis_color, status_font, DT_CENTER);
    draw_text(device_context, L"Y", RECT{678, toolbar.top + 16, 694, toolbar.top + 46},
        y_axis_color, status_font, DT_CENTER);
    draw_text(device_context, L"Z", RECT{702, toolbar.top + 16, 718, toolbar.top + 46},
        z_axis_color, status_font, DT_CENTER);

    if (scene_width(client_area) > 950)
    {
        wchar_t camera_label[48]{};
        std::swprintf(camera_label,
            sizeof(camera_label) / sizeof(camera_label[0]),
            L"CAMERA %.1f u", camera_state.distance);
        RECT camera_area{client_area.right - 210, toolbar.top + 16,
            client_area.right - 112, toolbar.top + 46};
        draw_text(device_context, camera_label, camera_area, muted_text_color,
            small_font, DT_RIGHT);

        wchar_t molecule_count[32]{};
        std::swprintf(molecule_count,
            sizeof(molecule_count) / sizeof(molecule_count[0]),
            L"%llu", static_cast<unsigned long long>(molecules.size()));
        RECT count_area{client_area.right - 104, toolbar.top + 16,
            client_area.right - 72, toolbar.top + 46};
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

    wchar_t fps_value[24]{};
    wchar_t molecule_value[24]{};
    wchar_t charge_value[24]{};
    wchar_t temperature_value[24]{};
    wchar_t energy_value[24]{};
    wchar_t frame_time_value[24]{};
    wchar_t camera_value[32]{};
    wchar_t uptime_value[24]{};
    std::swprintf(fps_value, sizeof(fps_value) / sizeof(fps_value[0]),
        L"%.0f", telemetry.frames_per_second);
    std::swprintf(molecule_value, sizeof(molecule_value) / sizeof(molecule_value[0]),
        L"%llu", static_cast<unsigned long long>(molecules.size()));
    if (selected_molecule_id > 0 &&
        selected_molecule_id <= simulation_engine.particles().size())
    {
        const double selected_charge = simulation_engine.particles()[
            static_cast<std::size_t>(selected_molecule_id - 1)].charge;
        std::swprintf(charge_value,
            sizeof(charge_value) / sizeof(charge_value[0]),
            L"%+.1f", selected_charge);
    }
    else
    {
        std::swprintf(charge_value,
            sizeof(charge_value) / sizeof(charge_value[0]), L"—");
    }
    std::swprintf(temperature_value,
        sizeof(temperature_value) / sizeof(temperature_value[0]),
        L"%.3f", simulation_engine.metrics().temperature);
    std::swprintf(energy_value,
        sizeof(energy_value) / sizeof(energy_value[0]),
        L"%.3f", simulation_engine.metrics().potential_energy);
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
    const Metric metrics[]{
        {L"FPS", fps_value},
        {L"MOLECULES", molecule_value},
        {L"STEPS / SEC", nullptr},
        {L"CHARGE", charge_value},
        {L"TEMP", temperature_value},
        {L"POTENTIAL", energy_value},
        {L"PING", L"N/A"},
        {L"FRAME", frame_time_value},
        {L"CAMERA SPEED", camera_value},
        {L"UPTIME", uptime_value}};
    constexpr int metric_count = static_cast<int>(sizeof(metrics) / sizeof(metrics[0]));
    wchar_t simulation_steps_value[24]{};
    std::swprintf(simulation_steps_value,
        sizeof(simulation_steps_value) / sizeof(simulation_steps_value[0]),
        L"%.0f", telemetry.simulation_steps_per_second);
    const int column_width = scene_width(client_area) / metric_count;
    for (int index = 0; index < metric_count; ++index)
    {
        const int left = client_area.left + index * column_width;
        const int right = index == metric_count - 1
            ? client_area.right
            : left + column_width;
        RECT label_area{left + 4, footer.top + 4, right - 4, footer.top + 22};
        RECT value_area{left + 4, footer.top + 23, right - 4, footer.top + 44};
        draw_text(device_context, metrics[index].label, label_area,
            muted_text_color, small_font, DT_CENTER);
        const wchar_t* metric_value = index == 2
            ? simulation_steps_value
            : metrics[index].value;
        draw_text(device_context, metric_value, value_area,
            index == 0 ? x_axis_color : primary_text_color,
            status_font, DT_CENTER);
        if (index > 0)
        {
            draw_line(device_context, left, footer.top + 10,
                left, footer.top + 39, border_color);
        }
    }

    draw_line(device_context, client_area.left, footer.top + 47,
        client_area.right, footer.top + 47, border_color);
    RECT instructions{16, footer.top + 49, client_area.right - 390, footer.bottom - 1};
    const wchar_t* hint = molecule_tool_active
        ? L"CLICK PLACE   SHIFT+WHEEL HEIGHT   ESC CANCEL"
        : bond_tool_active
        ? L"CLICK TWO MOLECULES TO BOND   ESC CANCEL"
        : L"WASD/QE MOVE   RMB ORBIT   MMB PAN   WHEEL ZOOM   SPACE SIM";
    draw_text(device_context, hint, instructions, muted_text_color, small_font);

    wchar_t coordinates[3][32]{};
    std::swprintf(coordinates[0], sizeof(coordinates[0]) / sizeof(coordinates[0][0]),
        L"X: %.2f", camera_state.target.x);
    std::swprintf(coordinates[1], sizeof(coordinates[1]) / sizeof(coordinates[1][0]),
        L"Y: %.2f", camera_state.target.y);
    std::swprintf(coordinates[2], sizeof(coordinates[2]) / sizeof(coordinates[2][0]),
        L"Z: %.2f", camera_state.target.z);
    RECT status_area{client_area.right - 378, footer.top + 49,
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

void draw_overlay(HDC device_context, const RECT& client_area)
{
    HFONT title_font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT small_font = CreateFontW(-10, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT button_font = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT status_font = CreateFontW(-11, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (title_font != nullptr && small_font != nullptr &&
        button_font != nullptr && status_font != nullptr)
    {
        draw_toolbar(device_context, client_area, title_font, small_font,
            button_font, status_font);
        draw_footer(device_context, client_area, small_font, status_font);
    }

    if (title_font != nullptr)
    {
        DeleteObject(title_font);
    }
    if (small_font != nullptr)
    {
        DeleteObject(small_font);
    }
    if (button_font != nullptr)
    {
        DeleteObject(button_font);
    }
    if (status_font != nullptr)
    {
        DeleteObject(status_font);
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

    const auto& particles = simulation_engine.particles();
    for (std::size_t index = 0;
         index < (std::min)(particles.size(), molecules.size()); ++index)
    {
        molecules[index].position = particles[index].position;
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
    Molecule* molecule = pick_molecule(client_area, screen);
    if (molecule == nullptr)
    {
        return;
    }
    selected_molecule_id = molecule->id;
    if (pending_bond_molecule_id == 0)
    {
        pending_bond_molecule_id = molecule->id;
        invalidate_simulation_ui(window, false);
        return;
    }
    if (pending_bond_molecule_id == molecule->id)
    {
        pending_bond_molecule_id = 0;
        invalidate_simulation_ui(window, false);
        return;
    }

    const std::size_t first =
        static_cast<std::size_t>(pending_bond_molecule_id - 1);
    const std::size_t second = static_cast<std::size_t>(molecule->id - 1);
    const Vec3 displacement =
        molecules[first].position - molecules[second].position;
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
    RECT client_area{};
    GetClientRect(window, &client_area);
    Vec3 world_position{};
    if (!screen_to_plane(client_area, position,
            molecule_placement_height, world_position))
    {
        return;
    }
    const std::uint64_t id = next_molecule_id++;
    molecules.push_back(Molecule{id, world_position, molecule_radius});
    const std::size_t particle_index =
        simulation_engine.add_particle(world_position);
    if (!simulation_engine.set_particle_properties(particle_index, 1.0, 0.0,
            molecule_sigma, molecule_epsilon, false))
    {
        molecules.pop_back();
        --next_molecule_id;
        MessageBoxW(window, L"Unable to initialize molecule physics properties.",
            L"Simulation error", MB_OK | MB_ICONERROR);
        return;
    }
    selected_molecule_id = id;
    molecule_dragging = true;
    molecule_drag_offset = Vec3{};
    SetCapture(window);
}

bool begin_molecule_drag(const RECT& client_area, POINT screen)
{
    molecule_dragging = false;
    Molecule* molecule = pick_molecule(client_area, screen);
    if (molecule == nullptr)
    {
        selected_molecule_id = 0;
        return false;
    }

    selected_molecule_id = molecule->id;
    Vec3 cursor_position{};
    if (screen_to_plane(client_area, screen, molecule->position.z, cursor_position))
    {
        molecule_drag_offset = molecule->position - cursor_position;
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

        draw_overlay(paint_context, client_area);
        EndPaint(window, &paint);
        if (scene_rendered)
        {
            record_rendered_frame(render_started_at);
        }
        return 0;
    }

    if (message == WM_KEYDOWN)
    {
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
            for (Molecule& molecule : molecules)
            {
                if (molecule.id != selected_molecule_id)
                {
                    continue;
                }
                Vec3 cursor_position{};
                if (screen_to_plane(client_area, position, molecule.position.z,
                        cursor_position))
                {
                    molecule.position = cursor_position + molecule_drag_offset;
                    const std::size_t particle_index =
                        static_cast<std::size_t>(molecule.id - 1);
                    if (!simulation_engine.set_position(
                            particle_index, molecule.position) ||
                        !simulation_engine.set_velocity(
                            particle_index, Vec3{}))
                    {
                        set_simulation_running(window, false);
                    }
                }
                break;
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
        molecule_dragging = false;
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
        molecule_dragging = false;
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
            if (begin_molecule_drag(client_area, position))
            {
                if (molecule_dragging)
                {
                    if (simulation_running)
                    {
                        set_simulation_running(window, false);
                    }
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
        ReleaseCapture();
    }

    if (message == WM_GETMINMAXINFO)
    {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        limits->ptMinTrackSize.x = 800;
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
