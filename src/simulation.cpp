#include "simulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <unordered_map>
#include <utility>

namespace simulation
{
namespace
{
constexpr double minimum_squared_distance = 1e-18;
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t barnes_hut_minimum_particles = 64;

Vec3 operator+(Vec3 left, Vec3 right)
{
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right)
{
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 value, double scalar)
{
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 operator/(Vec3 value, double scalar)
{
    return value * (1.0 / scalar);
}

Vec3& operator+=(Vec3& left, Vec3 right)
{
    left = left + right;
    return left;
}

double dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length_squared(Vec3 value)
{
    return dot(value, value);
}

bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

struct Cell
{
    std::int64_t x;
    std::int64_t y;
    std::int64_t z;

    bool operator==(const Cell& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct CellHash
{
    std::size_t operator()(const Cell& cell) const
    {
        const std::size_t x = std::hash<std::int64_t>{}(cell.x);
        const std::size_t y = std::hash<std::int64_t>{}(cell.y);
        const std::size_t z = std::hash<std::int64_t>{}(cell.z);
        return x ^ (y + 0x9e3779b9U + (x << 6U) + (x >> 2U)) ^
            (z + 0x9e3779b9U + (y << 6U) + (y >> 2U));
    }
};

bool to_cell_coordinate(double position, double cell_size, std::int64_t& result)
{
    const double coordinate = std::floor(position / cell_size);
    if (!std::isfinite(coordinate) ||
        coordinate <= static_cast<double>((std::numeric_limits<std::int64_t>::min)()) ||
        coordinate >= static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
    {
        return false;
    }
    result = static_cast<std::int64_t>(coordinate);
    return true;
}

void add_pair_force(std::vector<Particle>& particles, std::size_t first,
    std::size_t second, Vec3 force_on_first)
{
    particles[first].force += force_on_first;
    particles[second].force += force_on_first * -1.0;
}

struct TreeNode
{
    Vec3 center;
    double half_size = 0.0;
    double total_charge = 0.0;
    double absolute_charge = 0.0;
    Vec3 charge_center;
    Vec3 charge_weighted_position;
    Vec3 absolute_weighted_position;
    std::vector<std::size_t> members;
    std::array<int, 8> children{
        -1, -1, -1, -1, -1, -1, -1, -1};
};

class BarnesHutTree
{
public:
    BarnesHutTree(const std::vector<Particle>& particles,
        const std::vector<std::size_t>& particle_indices, double theta)
        : particles_(particles), theta_(theta)
    {
        if (particle_indices.empty())
        {
            return;
        }

        Vec3 minimum = particles[particle_indices.front()].position;
        Vec3 maximum = minimum;
        for (const std::size_t index : particle_indices)
        {
            const Particle& particle = particles[index];
            minimum.x = (std::min)(minimum.x, particle.position.x);
            minimum.y = (std::min)(minimum.y, particle.position.y);
            minimum.z = (std::min)(minimum.z, particle.position.z);
            maximum.x = (std::max)(maximum.x, particle.position.x);
            maximum.y = (std::max)(maximum.y, particle.position.y);
            maximum.z = (std::max)(maximum.z, particle.position.z);
        }

        const Vec3 center = (minimum + maximum) * 0.5;
        const double extent = (std::max)({
            maximum.x - minimum.x, maximum.y - minimum.y,
            maximum.z - minimum.z, 1e-6});
        nodes_.reserve(particle_indices.size() * 2);
        TreeNode root;
        root.center = center;
        root.half_size = extent * 0.5 + 1e-9;
        nodes_.push_back(root);
        for (const std::size_t index : particle_indices)
        {
            insert(0, index, 0);
        }
        aggregate(0);
    }

    bool accumulate(std::size_t particle_index, double coulomb_constant,
        Vec3& force, double& potential) const
    {
        if (nodes_.empty())
        {
            return true;
        }
        return accumulate_node(
            0, particle_index, coulomb_constant, force, potential);
    }

private:
    static constexpr std::size_t leaf_capacity = 8;
    static constexpr unsigned int maximum_depth = 32;

    bool is_leaf(const TreeNode& node) const
    {
        return std::all_of(node.children.begin(), node.children.end(),
            [](int child) { return child < 0; });
    }

    unsigned int octant(const TreeNode& node, Vec3 position) const
    {
        return (position.x >= node.center.x ? 1U : 0U) |
            (position.y >= node.center.y ? 2U : 0U) |
            (position.z >= node.center.z ? 4U : 0U);
    }

    int create_child(std::size_t parent_index, unsigned int child_octant)
    {
        const TreeNode parent = nodes_[parent_index];
        const double child_half = parent.half_size * 0.5;
        const Vec3 child_center{
            parent.center.x + ((child_octant & 1U) != 0 ? child_half : -child_half),
            parent.center.y + ((child_octant & 2U) != 0 ? child_half : -child_half),
            parent.center.z + ((child_octant & 4U) != 0 ? child_half : -child_half)};
        const int child_index = static_cast<int>(nodes_.size());
        TreeNode child;
        child.center = child_center;
        child.half_size = child_half;
        nodes_.push_back(child);
        nodes_[parent_index].children[child_octant] = child_index;
        return child_index;
    }

    void insert(std::size_t node_index, std::size_t particle_index,
        unsigned int depth)
    {
        if (is_leaf(nodes_[node_index]) &&
            (nodes_[node_index].members.size() < leaf_capacity ||
                depth >= maximum_depth))
        {
            nodes_[node_index].members.push_back(particle_index);
            return;
        }

        if (is_leaf(nodes_[node_index]))
        {
            const std::vector<std::size_t> previous_members =
                nodes_[node_index].members;
            nodes_[node_index].members.clear();
            for (const std::size_t previous : previous_members)
            {
                const unsigned int previous_octant =
                    octant(nodes_[node_index], particles_[previous].position);
                const int child = nodes_[node_index].children[previous_octant] >= 0
                    ? nodes_[node_index].children[previous_octant]
                    : create_child(node_index, previous_octant);
                insert(static_cast<std::size_t>(child), previous, depth + 1);
            }
        }

        const unsigned int destination =
            octant(nodes_[node_index], particles_[particle_index].position);
        const int child = nodes_[node_index].children[destination] >= 0
            ? nodes_[node_index].children[destination]
            : create_child(node_index, destination);
        insert(static_cast<std::size_t>(child), particle_index, depth + 1);
    }

    void aggregate(std::size_t node_index)
    {
        TreeNode& node = nodes_[node_index];
        if (is_leaf(node))
        {
            for (const std::size_t index : node.members)
            {
                const Particle& particle = particles_[index];
                node.total_charge += particle.charge;
                node.absolute_charge += std::abs(particle.charge);
                node.charge_weighted_position +=
                    particle.position * particle.charge;
                node.absolute_weighted_position +=
                    particle.position * std::abs(particle.charge);
            }
        }
        else
        {
            for (const int child_index : node.children)
            {
                if (child_index < 0)
                {
                    continue;
                }
                aggregate(static_cast<std::size_t>(child_index));
                const TreeNode& child =
                    nodes_[static_cast<std::size_t>(child_index)];
                node.total_charge += child.total_charge;
                node.absolute_charge += child.absolute_charge;
                node.charge_weighted_position +=
                    child.charge_weighted_position;
                node.absolute_weighted_position +=
                    child.absolute_weighted_position;
            }
        }
        if (node.absolute_charge > 0.0)
        {
            node.charge_center = std::abs(node.total_charge) > 1e-12
                ? node.charge_weighted_position / node.total_charge
                : node.absolute_weighted_position / node.absolute_charge;
        }
    }

    bool contains(const TreeNode& node, Vec3 point) const
    {
        return std::abs(point.x - node.center.x) <= node.half_size &&
            std::abs(point.y - node.center.y) <= node.half_size &&
            std::abs(point.z - node.center.z) <= node.half_size;
    }

    bool accumulate_node(std::size_t node_index, std::size_t target_index,
        double coulomb_constant, Vec3& force, double& potential) const
    {
        const TreeNode& node = nodes_[node_index];
        const Particle& target = particles_[target_index];
        if (std::abs(node.total_charge) < 1e-15 || node.absolute_charge == 0.0)
        {
            return true;
        }

        if (is_leaf(node))
        {
            for (const std::size_t source_index : node.members)
            {
                if (source_index == target_index)
                {
                    continue;
                }
                const Particle& source = particles_[source_index];
                if (source.charge == 0.0)
                {
                    continue;
                }
                const Vec3 displacement = target.position - source.position;
                const double distance_squared = length_squared(displacement);
                if (distance_squared < minimum_squared_distance)
                {
                    return false;
                }
                const double distance = std::sqrt(distance_squared);
                const double coefficient = coulomb_constant *
                    target.charge * source.charge;
                force += displacement *
                    (coefficient / (distance_squared * distance));
                potential += coefficient / distance;
            }
            return true;
        }

        const Vec3 displacement = target.position - node.charge_center;
        const double distance_squared = length_squared(displacement);
        const double distance = std::sqrt(distance_squared);
        const double node_width = node.half_size * 2.0;
        if (distance_squared > minimum_squared_distance &&
            !contains(node, target.position) &&
            node_width / distance < theta_)
        {
            const double coefficient =
                coulomb_constant * target.charge * node.total_charge;
            force += displacement *
                (coefficient / (distance_squared * distance));
            potential += coefficient / distance;
            return true;
        }

        for (const int child : node.children)
        {
            if (child >= 0)
            {
                if (!accumulate_node(static_cast<std::size_t>(child),
                        target_index, coulomb_constant, force, potential))
                {
                    return false;
                }
            }
        }
        return true;
    }

    const std::vector<Particle>& particles_;
    double theta_;
    std::vector<TreeNode> nodes_;
};

double kinetic_energy(const std::vector<Particle>& particles)
{
    double energy = 0.0;
    for (const Particle& particle : particles)
    {
        if (!particle.fixed)
        {
            energy += 0.5 * particle.mass * length_squared(particle.velocity);
        }
    }
    return energy;
}
}

std::size_t Engine::add_particle(Vec3 position)
{
    Particle particle;
    particle.position = position;
    particles_.push_back(particle);
    invalidate_neighbors();
    charged_particles_dirty_ = true;
    forces_initialized_ = false;
    return particles_.size() - 1;
}

bool Engine::add_bond(Bond bond, std::string& error)
{
    if (bond.first >= particles_.size() || bond.second >= particles_.size() ||
        bond.first == bond.second || !std::isfinite(bond.equilibrium_length) ||
        bond.equilibrium_length <= 0.0 || !std::isfinite(bond.stiffness) ||
        bond.stiffness < 0.0)
    {
        error = "Invalid bond parameters or particle indices.";
        return false;
    }
    for (const Bond& existing : bonds_)
    {
        if ((existing.first == bond.first && existing.second == bond.second) ||
            (existing.first == bond.second && existing.second == bond.first))
        {
            error = "A bond already connects these particles.";
            return false;
        }
    }

    const std::vector<Bond> existing_bonds = bonds_;
    bonds_.push_back(bond);
    for (const Bond& existing : existing_bonds)
    {
        std::size_t center = 0;
        std::size_t first = 0;
        std::size_t third = 0;
        if (existing.first == bond.first)
        {
            center = bond.first;
            first = existing.second;
            third = bond.second;
        }
        else if (existing.second == bond.first)
        {
            center = bond.first;
            first = existing.first;
            third = bond.second;
        }
        else if (existing.first == bond.second)
        {
            center = bond.second;
            first = bond.first;
            third = existing.second;
        }
        else if (existing.second == bond.second)
        {
            center = bond.second;
            first = bond.first;
            third = existing.first;
        }
        else
        {
            continue;
        }

        const Vec3 first_arm =
            particles_[first].position - particles_[center].position;
        const Vec3 third_arm =
            particles_[third].position - particles_[center].position;
        const double arm_product =
            std::sqrt(length_squared(first_arm) * length_squared(third_arm));
        if (arm_product < 1e-18)
        {
            continue;
        }
        const double equilibrium_angle = std::acos((std::clamp)(
            dot(first_arm, third_arm) / arm_product, -1.0, 1.0));
        const bool angle_exists = std::any_of(angles_.begin(), angles_.end(),
            [center, first, third](const Angle& angle) {
                return angle.center == center &&
                    ((angle.first == first && angle.third == third) ||
                        (angle.first == third && angle.third == first));
            });
        if (!angle_exists)
        {
            angles_.push_back(
                Angle{first, center, third, equilibrium_angle, 2.0});
        }
    }
    forces_initialized_ = false;
    return true;
}

bool Engine::add_angle(Angle angle, std::string& error)
{
    if (angle.first >= particles_.size() || angle.center >= particles_.size() ||
        angle.third >= particles_.size() || angle.first == angle.center ||
        angle.third == angle.center || angle.first == angle.third ||
        !std::isfinite(angle.equilibrium_radians) ||
        angle.equilibrium_radians < 0.0 || angle.equilibrium_radians > pi ||
        !std::isfinite(angle.stiffness) || angle.stiffness < 0.0)
    {
        error = "Invalid angle parameters or particle indices.";
        return false;
    }
    angles_.push_back(angle);
    forces_initialized_ = false;
    return true;
}

bool Engine::set_position(std::size_t index, Vec3 position)
{
    if (index >= particles_.size() || !finite(position))
    {
        return false;
    }
    particles_[index].position = position;
    forces_initialized_ = false;
    return true;
}

bool Engine::set_velocity(std::size_t index, Vec3 velocity)
{
    if (index >= particles_.size() || !finite(velocity))
    {
        return false;
    }
    particles_[index].velocity = velocity;
    return true;
}

bool Engine::set_external_force(std::size_t index, Vec3 force)
{
    if (index >= particles_.size() || !finite(force))
    {
        return false;
    }
    Particle& particle = particles_[index];
    if (forces_initialized_)
    {
        particle.force += force - particle.external_force;
    }
    particle.external_force = force;
    return true;
}

bool Engine::set_charge(std::size_t index, double charge)
{
    if (index >= particles_.size() || !std::isfinite(charge))
    {
        return false;
    }
    if (particles_[index].charge != charge)
    {
        particles_[index].charge = charge;
        charged_particles_dirty_ = true;
    }
    forces_initialized_ = false;
    return true;
}

bool Engine::set_particle_properties(std::size_t index, double mass,
    double charge, double sigma, double epsilon, bool fixed)
{
    if (index >= particles_.size() || !std::isfinite(mass) || mass <= 0.0 ||
        !std::isfinite(charge) || !std::isfinite(sigma) || sigma <= 0.0 ||
        !std::isfinite(epsilon) || epsilon < 0.0)
    {
        return false;
    }
    Particle& particle = particles_[index];
    if (particle.charge != charge)
    {
        charged_particles_dirty_ = true;
    }
    particle.mass = mass;
    particle.charge = charge;
    particle.sigma = sigma;
    particle.epsilon = epsilon;
    particle.fixed = fixed;
    forces_initialized_ = false;
    invalidate_neighbors();
    return true;
}

void Engine::clear()
{
    std::vector<Particle>().swap(particles_);
    std::vector<Bond>().swap(bonds_);
    std::vector<Angle>().swap(angles_);
    std::vector<NeighborPair>().swap(neighbor_pairs_);
    std::vector<std::size_t>().swap(charged_particles_);
    charged_particles_dirty_ = false;
    std::vector<Vec3>().swap(neighbor_reference_positions_);
    metrics_ = Metrics{};
    neighbor_reference_cutoff_ = -1.0;
    neighbor_reference_skin_ = -1.0;
    forces_initialized_ = false;
}

void Engine::invalidate_neighbors()
{
    neighbor_reference_positions_.clear();
}

void Engine::invalidate_forces()
{
    forces_initialized_ = false;
}

bool Engine::validate_config(std::string& error) const
{
    if (!std::isfinite(config_.time_step) || config_.time_step <= 0.0 ||
        config_.time_step > 0.02)
    {
        error = "The simulation time step must be between 0 and 0.02.";
        return false;
    }
    if (!std::isfinite(config_.cutoff) || config_.cutoff <= 0.0 ||
        !std::isfinite(config_.neighbor_skin) || config_.neighbor_skin < 0.0 ||
        !std::isfinite(config_.cutoff + config_.neighbor_skin) ||
        config_.cutoff + config_.neighbor_skin > 1e9)
    {
        error = "The neighbor cutoff and skin must be valid non-negative values.";
        return false;
    }
    if (!std::isfinite(config_.target_temperature) ||
        config_.target_temperature < 0.0 ||
        !std::isfinite(config_.thermostat_rate) ||
        config_.thermostat_rate < 0.0 ||
        !std::isfinite(config_.gravity_z) ||
        !std::isfinite(config_.coulomb_constant) ||
        config_.coulomb_constant < 0.0 ||
        !std::isfinite(config_.barnes_hut_theta) ||
        config_.barnes_hut_theta <= 0.0 ||
        !std::isfinite(config_.wall_stiffness) ||
        config_.wall_stiffness < 0.0 ||
        !std::isfinite(config_.boundary_min) ||
        !std::isfinite(config_.boundary_max) ||
        config_.boundary_min >= config_.boundary_max ||
        !std::isfinite(config_.restitution) ||
        config_.restitution < 0.0 || config_.restitution > 1.0 ||
        !std::isfinite(config_.maximum_speed) ||
        config_.maximum_speed < 0.0)
    {
        error = "One or more simulation parameters are outside their valid range.";
        return false;
    }
    if (config_.thermostat == Thermostat::berendsen &&
        config_.thermostat_rate <= 0.0)
    {
        error = "The Berendsen thermostat requires a positive relaxation time.";
        return false;
    }
    for (const Particle& particle : particles_)
    {
        if (!finite(particle.position) || !finite(particle.velocity) ||
            !std::isfinite(particle.mass) || particle.mass <= 0.0 ||
            !std::isfinite(particle.charge) ||
            !std::isfinite(particle.sigma) || particle.sigma <= 0.0 ||
            !std::isfinite(particle.epsilon) || particle.epsilon < 0.0)
        {
            error = "A particle has invalid position, velocity, or physical parameters.";
            return false;
        }
    }
    return true;
}

bool Engine::force_configuration_matches() const
{
    return force_config_snapshot_.cutoff == config_.cutoff &&
        force_config_snapshot_.neighbor_skin == config_.neighbor_skin &&
        force_config_snapshot_.gravity_z == config_.gravity_z &&
        force_config_snapshot_.coulomb_constant == config_.coulomb_constant &&
        force_config_snapshot_.barnes_hut_theta ==
            config_.barnes_hut_theta &&
        force_config_snapshot_.wall_stiffness == config_.wall_stiffness &&
        force_config_snapshot_.boundary_min == config_.boundary_min &&
        force_config_snapshot_.boundary_max == config_.boundary_max &&
        force_config_snapshot_.boundary_mode == config_.boundary_mode &&
        force_config_snapshot_.coulomb_solver == config_.coulomb_solver;
}

bool Engine::needs_neighbor_rebuild() const
{
    if (neighbor_reference_positions_.size() != particles_.size() ||
        neighbor_reference_positions_.empty() ||
        neighbor_reference_cutoff_ != config_.cutoff ||
        neighbor_reference_skin_ != config_.neighbor_skin)
    {
        return true;
    }
    if (config_.neighbor_skin == 0.0)
    {
        for (std::size_t index = 0; index < particles_.size(); ++index)
        {
            if (particles_[index].position.x != neighbor_reference_positions_[index].x ||
                particles_[index].position.y != neighbor_reference_positions_[index].y ||
                particles_[index].position.z != neighbor_reference_positions_[index].z)
            {
                return true;
            }
        }
        return false;
    }
    const double displacement_limit_squared =
        config_.neighbor_skin * config_.neighbor_skin * 0.25;
    for (std::size_t index = 0; index < particles_.size(); ++index)
    {
        if (length_squared(particles_[index].position -
                neighbor_reference_positions_[index]) >=
            displacement_limit_squared)
        {
            return true;
        }
    }
    return false;
}

void Engine::rebuild_neighbor_list()
{
    neighbor_pairs_.clear();
    const double radius = config_.cutoff + config_.neighbor_skin;
    const double radius_squared = radius * radius;
    std::unordered_map<Cell, std::vector<std::size_t>, CellHash> cells;
    for (std::size_t index = 0; index < particles_.size(); ++index)
    {
        const Vec3 position = particles_[index].position;
        Cell cell{};
        if (!to_cell_coordinate(position.x, radius, cell.x) ||
            !to_cell_coordinate(position.y, radius, cell.y) ||
            !to_cell_coordinate(position.z, radius, cell.z))
        {
            neighbor_reference_positions_.clear();
            return;
        }
        if (cell.x == (std::numeric_limits<std::int64_t>::min)() ||
            cell.x == (std::numeric_limits<std::int64_t>::max)() ||
            cell.y == (std::numeric_limits<std::int64_t>::min)() ||
            cell.y == (std::numeric_limits<std::int64_t>::max)() ||
            cell.z == (std::numeric_limits<std::int64_t>::min)() ||
            cell.z == (std::numeric_limits<std::int64_t>::max)())
        {
            neighbor_reference_positions_.clear();
            return;
        }
        cells[cell].push_back(index);
    }

    for (std::size_t index = 0; index < particles_.size(); ++index)
    {
        Cell cell{};
        const Vec3 position = particles_[index].position;
        if (!to_cell_coordinate(position.x, radius, cell.x) ||
            !to_cell_coordinate(position.y, radius, cell.y) ||
            !to_cell_coordinate(position.z, radius, cell.z))
        {
            neighbor_reference_positions_.clear();
            return;
        }
        if (cell.x == (std::numeric_limits<std::int64_t>::min)() ||
            cell.x == (std::numeric_limits<std::int64_t>::max)() ||
            cell.y == (std::numeric_limits<std::int64_t>::min)() ||
            cell.y == (std::numeric_limits<std::int64_t>::max)() ||
            cell.z == (std::numeric_limits<std::int64_t>::min)() ||
            cell.z == (std::numeric_limits<std::int64_t>::max)())
        {
            neighbor_reference_positions_.clear();
            return;
        }
        for (std::int64_t z = -1; z <= 1; ++z)
        {
            for (std::int64_t y = -1; y <= 1; ++y)
            {
                for (std::int64_t x = -1; x <= 1; ++x)
                {
                    const auto found = cells.find(
                        Cell{cell.x + x, cell.y + y, cell.z + z});
                    if (found == cells.end())
                    {
                        continue;
                    }
                    for (const std::size_t other : found->second)
                    {
                        if (other > index &&
                            length_squared(position - particles_[other].position) <=
                                radius_squared)
                        {
                            neighbor_pairs_.push_back(NeighborPair{
                                index, other});
                        }
                    }
                }
            }
        }
    }

    neighbor_reference_positions_.clear();
    neighbor_reference_positions_.reserve(particles_.size());
    for (const Particle& particle : particles_)
    {
        neighbor_reference_positions_.push_back(particle.position);
    }
    neighbor_reference_cutoff_ = config_.cutoff;
    neighbor_reference_skin_ = config_.neighbor_skin;
    ++metrics_.neighbor_rebuilds;
}

bool Engine::compute_forces(std::string& error)
{
    if (needs_neighbor_rebuild())
    {
        rebuild_neighbor_list();
        if (neighbor_reference_positions_.size() != particles_.size())
        {
            error = "Particle coordinates exceed the supported neighbor-grid range.";
            return false;
        }
    }

    for (Particle& particle : particles_)
    {
        particle.force = particle.external_force +
            Vec3{0.0, 0.0, particle.mass * config_.gravity_z};
    }

    double potential_energy = 0.0;
    double virial = 0.0;
    const double cutoff_squared = config_.cutoff * config_.cutoff;
    for (const NeighborPair& pair : neighbor_pairs_)
    {
        const std::size_t first_index = pair.first;
        const std::size_t second_index = pair.second;
        Particle& first = particles_[first_index];
        Particle& second = particles_[second_index];
        const double sigma = 0.5 * (first.sigma + second.sigma);
        const double epsilon = std::sqrt(first.epsilon * second.epsilon);
        const Vec3 displacement = first.position - second.position;
        const double distance_squared = length_squared(displacement);
        if (distance_squared < minimum_squared_distance)
        {
            if (epsilon > 0.0)
            {
                error = "Two interacting molecules occupy the same position.";
                return false;
            }
            continue;
        }
        if (distance_squared > cutoff_squared)
        {
            continue;
        }

        const double distance = std::sqrt(distance_squared);
        const double sigma_over_distance = sigma / distance;
        const double ratio_squared = sigma_over_distance * sigma_over_distance;
        const double ratio_six = ratio_squared * ratio_squared * ratio_squared;
        const double ratio_twelve = ratio_six * ratio_six;
        potential_energy += 4.0 * epsilon * (ratio_twelve - ratio_six);
        const double force_scale =
            24.0 * epsilon * (2.0 * ratio_twelve - ratio_six) /
            distance_squared;
        const Vec3 force = displacement * force_scale;
        add_pair_force(particles_, first_index, second_index, force);
        virial += dot(displacement, force);
    }

    if (charged_particles_dirty_)
    {
        std::size_t charged_count = 0;
        for (const Particle& particle : particles_)
        {
            charged_count += particle.charge != 0.0 ? 1U : 0U;
        }
        const bool release_excess_capacity =
            charged_particles_.capacity() > charged_count &&
            charged_particles_.capacity() - charged_count > charged_count + 64;
        std::vector<std::size_t> compacted_charges;
        if (release_excess_capacity)
        {
            compacted_charges.reserve(charged_count);
        }
        else
        {
            charged_particles_.clear();
            if (charged_particles_.capacity() < charged_count)
            {
                charged_particles_.reserve(charged_count);
            }
        }
        for (std::size_t index = 0; index < particles_.size(); ++index)
        {
            if (particles_[index].charge != 0.0)
            {
                if (release_excess_capacity)
                {
                    compacted_charges.push_back(index);
                }
                else
                {
                    charged_particles_.push_back(index);
                }
            }
        }
        if (release_excess_capacity)
        {
            charged_particles_.swap(compacted_charges);
        }
        charged_particles_dirty_ = false;
    }

    if (!charged_particles_.empty() &&
        config_.coulomb_solver == CoulombSolver::barnes_hut &&
        charged_particles_.size() >= barnes_hut_minimum_particles)
    {
        const BarnesHutTree tree(
            particles_, charged_particles_, config_.barnes_hut_theta);
        Vec3 origin{};
        for (const Particle& particle : particles_)
        {
            origin += particle.position;
        }
        origin = origin / static_cast<double>(particles_.size());
        for (const std::size_t index : charged_particles_)
        {
            Vec3 force{};
            double particle_potential = 0.0;
            if (!tree.accumulate(index, config_.coulomb_constant,
                    force, particle_potential))
            {
                error = "Two charged molecules occupy the same position.";
                return false;
            }
            particles_[index].force += force;
            potential_energy += 0.5 * particle_potential;
            virial += 0.5 * dot(
                particles_[index].position - origin, force);
        }
    }
    else if (!charged_particles_.empty())
    {
        for (std::size_t first_position = 0;
             first_position < charged_particles_.size(); ++first_position)
        {
            for (std::size_t second_position = first_position + 1;
                 second_position < charged_particles_.size(); ++second_position)
            {
                const std::size_t first_index =
                    charged_particles_[first_position];
                const std::size_t second_index =
                    charged_particles_[second_position];
                const Particle& first = particles_[first_index];
                const Particle& second = particles_[second_index];
                const Vec3 displacement = first.position - second.position;
                const double distance_squared = length_squared(displacement);
                if (distance_squared < minimum_squared_distance)
                {
                    error = "Two charged molecules occupy the same position.";
                    return false;
                }
                const double distance = std::sqrt(distance_squared);
                const double coefficient = config_.coulomb_constant *
                    first.charge * second.charge;
                potential_energy += coefficient / distance;
                const Vec3 force = displacement * (coefficient /
                    (distance_squared * distance));
                add_pair_force(particles_, first_index, second_index, force);
                virial += dot(displacement, force);
            }
        }
    }

    for (const Bond& bond : bonds_)
    {
        Particle& first = particles_[bond.first];
        Particle& second = particles_[bond.second];
        const Vec3 displacement = first.position - second.position;
        const double distance = std::sqrt(length_squared(displacement));
        if (distance < 1e-9)
        {
            error = "A bonded molecule pair occupies the same position.";
            return false;
        }
        const double extension = distance - bond.equilibrium_length;
        potential_energy += 0.5 * bond.stiffness * extension * extension;
        const Vec3 force = displacement *
            (-bond.stiffness * extension / distance);
        add_pair_force(particles_, bond.first, bond.second, force);
        virial += dot(displacement, force);
    }

    for (const Angle& angle : angles_)
    {
        Particle& first = particles_[angle.first];
        Particle& center = particles_[angle.center];
        Particle& third = particles_[angle.third];
        const Vec3 first_arm = first.position - center.position;
        const Vec3 third_arm = third.position - center.position;
        const double first_length = std::sqrt(length_squared(first_arm));
        const double third_length = std::sqrt(length_squared(third_arm));
        if (first_length < 1e-9 || third_length < 1e-9)
        {
            continue;
        }

        const double cosine = (std::clamp)(
            dot(first_arm, third_arm) / (first_length * third_length),
            -1.0, 1.0);
        const double radians = std::acos(cosine);
        const double sine = std::sqrt((std::max)(0.0, 1.0 - cosine * cosine));
        if (sine < 1e-9)
        {
            continue;
        }
        const double deviation = radians - angle.equilibrium_radians;
        potential_energy += 0.5 * angle.stiffness * deviation * deviation;
        const Vec3 first_gradient =
            (third_arm / (first_length * third_length) -
                first_arm * (cosine / (first_length * first_length))) *
            (-1.0 / sine);
        const Vec3 third_gradient =
            (first_arm / (first_length * third_length) -
                third_arm * (cosine / (third_length * third_length))) *
            (-1.0 / sine);
        const Vec3 first_force =
            first_gradient * (-angle.stiffness * deviation);
        const Vec3 third_force =
            third_gradient * (-angle.stiffness * deviation);
        first.force += first_force;
        third.force += third_force;
        center.force += (first_force + third_force) * -1.0;
        virial += dot(first_arm, first_force) +
            dot(third_arm, third_force);
    }

    for (std::size_t index = 0; index < particles_.size(); ++index)
    {
        Particle& particle = particles_[index];
        if (config_.boundary_mode == BoundaryMode::soft)
        {
            const double distances[]{
                config_.boundary_min - particle.position.x,
                particle.position.x - config_.boundary_max,
                config_.boundary_min - particle.position.y,
                particle.position.y - config_.boundary_max,
                config_.boundary_min - particle.position.z,
                particle.position.z - config_.boundary_max};
            for (unsigned int axis = 0; axis < 3; ++axis)
            {
                for (unsigned int side = 0; side < 2; ++side)
                {
                    const double distance = distances[axis * 2 + side];
                    if (distance <= 0.0)
                    {
                        continue;
                    }
                    const double force_magnitude = config_.wall_stiffness *
                        std::pow(distance, 6.0);
                    const double energy = config_.wall_stiffness *
                        std::pow(distance, 7.0) / 7.0;
                    if (axis == 0)
                    {
                        particle.force.x += side == 0
                            ? force_magnitude : -force_magnitude;
                    }
                    else if (axis == 1)
                    {
                        particle.force.y += side == 0
                            ? force_magnitude : -force_magnitude;
                    }
                    else
                    {
                        particle.force.z += side == 0
                            ? force_magnitude : -force_magnitude;
                    }
                    const double coordinate = axis == 0
                        ? particle.position.x
                        : axis == 1 ? particle.position.y : particle.position.z;
                    const double boundary_center =
                        (config_.boundary_min + config_.boundary_max) * 0.5;
                    virial += (coordinate - boundary_center) *
                        (side == 0 ? force_magnitude : -force_magnitude);
                    potential_energy += energy;
                }
            }
        }
        potential_energy -= particle.mass * config_.gravity_z *
            particle.position.z;
    }

    metrics_.potential_energy = potential_energy;
    metrics_.configurational_virial = virial;
    return true;
}

void Engine::apply_boundaries()
{
    if (config_.boundary_mode != BoundaryMode::reflective)
    {
        return;
    }
    for (Particle& particle : particles_)
    {
        if (particle.fixed)
        {
            continue;
        }
        double* coordinates[]{&particle.position.x, &particle.position.y,
            &particle.position.z};
        double* velocities[]{&particle.velocity.x, &particle.velocity.y,
            &particle.velocity.z};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            if (*coordinates[axis] < config_.boundary_min)
            {
                *coordinates[axis] = config_.boundary_min;
                *velocities[axis] = std::abs(*velocities[axis]) *
                    config_.restitution;
            }
            else if (*coordinates[axis] > config_.boundary_max)
            {
                *coordinates[axis] = config_.boundary_max;
                *velocities[axis] = -std::abs(*velocities[axis]) *
                    config_.restitution;
            }
        }
    }
}

void Engine::apply_thermostat()
{
    if (config_.thermostat == Thermostat::none || particles_.empty())
    {
        return;
    }

    std::size_t mobile_count = 0;
    for (const Particle& particle : particles_)
    {
        if (!particle.fixed)
        {
            ++mobile_count;
        }
    }
    if (mobile_count == 0)
    {
        return;
    }

    const double temperature = 2.0 * kinetic_energy(particles_) /
        (3.0 * static_cast<double>(mobile_count));
    if (config_.thermostat == Thermostat::andersen)
    {
        static thread_local std::mt19937_64 generator(std::random_device{}());
        std::uniform_real_distribution<double> probability(0.0, 1.0);
        const double collision_probability =
            1.0 - std::exp(-config_.thermostat_rate * config_.time_step);
        for (Particle& particle : particles_)
        {
            if (particle.fixed || probability(generator) >= collision_probability)
            {
                continue;
            }
            const double standard_deviation =
                std::sqrt(config_.target_temperature / particle.mass);
            std::normal_distribution<double> velocity(0.0, standard_deviation);
            particle.velocity = Vec3{
                velocity(generator), velocity(generator), velocity(generator)};
        }
    }
    else if (config_.thermostat == Thermostat::berendsen &&
        temperature > 1e-12)
    {
        const double factor_squared = (std::max)(0.0,
            1.0 + config_.time_step / config_.thermostat_rate *
                (config_.target_temperature / temperature - 1.0));
        const double scale = std::sqrt(factor_squared);
        for (Particle& particle : particles_)
        {
            if (!particle.fixed)
            {
                particle.velocity = particle.velocity * scale;
            }
        }
    }
    else if (config_.thermostat == Thermostat::acceleration_damping &&
        temperature > config_.target_temperature)
    {
        const double scale =
            std::exp(-config_.thermostat_rate * config_.time_step);
        for (Particle& particle : particles_)
        {
            if (!particle.fixed)
            {
                particle.velocity = particle.velocity * scale;
            }
        }
    }
}

void Engine::update_metrics()
{
    metrics_.kinetic_energy = kinetic_energy(particles_);
    metrics_.total_energy =
        metrics_.kinetic_energy + metrics_.potential_energy;
    metrics_.temperature = 0.0;
    metrics_.pressure = 0.0;
    metrics_.number_density = 0.0;
    metrics_.pressure_available = false;
    std::size_t mobile_count = 0;
    for (const Particle& particle : particles_)
    {
        if (!particle.fixed)
        {
            ++mobile_count;
        }
    }
    if (mobile_count > 0)
    {
        metrics_.temperature = metrics_.kinetic_energy * 2.0 /
            (3.0 * static_cast<double>(mobile_count));
    }
    if (config_.boundary_mode != BoundaryMode::none)
    {
        const double length = config_.boundary_max - config_.boundary_min;
        const double volume = length * length * length;
        metrics_.number_density =
            static_cast<double>(particles_.size()) / volume;
        if (!particles_.empty())
        {
            metrics_.pressure =
                (2.0 * metrics_.kinetic_energy +
                    metrics_.configurational_virial) / (3.0 * volume);
            metrics_.pressure_available = true;
        }
    }
    metrics_.neighbor_pairs = neighbor_pairs_.size();
}

bool Engine::step(std::string& error)
{
    if (!validate_config(error))
    {
        return false;
    }
    if (particles_.empty())
    {
        metrics_.steps = 0;
        update_metrics();
        return true;
    }
    if (forces_initialized_ && !force_configuration_matches())
    {
        forces_initialized_ = false;
    }
    if (!forces_initialized_ && !compute_forces(error))
    {
        return false;
    }

    const double half_time_step = config_.time_step * 0.5;
    for (Particle& particle : particles_)
    {
        if (particle.fixed)
        {
            continue;
        }
        const Vec3 acceleration = particle.force / particle.mass;
        if (config_.integrator == Integrator::velocity_verlet)
        {
            particle.position += particle.velocity * config_.time_step +
                acceleration * (0.5 * config_.time_step * config_.time_step);
            particle.velocity += acceleration * half_time_step;
        }
        else
        {
            particle.velocity += acceleration * half_time_step;
            particle.position += particle.velocity * config_.time_step;
        }
    }
    apply_boundaries();
    if (!compute_forces(error))
    {
        return false;
    }
    for (Particle& particle : particles_)
    {
        if (!particle.fixed)
        {
            particle.velocity +=
                (particle.force / particle.mass) * half_time_step;
        }
    }
    apply_thermostat();
    if (config_.maximum_speed > 0.0)
    {
        const double maximum_speed_squared =
            config_.maximum_speed * config_.maximum_speed;
        for (Particle& particle : particles_)
        {
            if (particle.fixed)
            {
                continue;
            }
            const double speed_squared = length_squared(particle.velocity);
            if (speed_squared > maximum_speed_squared)
            {
                particle.velocity = particle.velocity *
                    (config_.maximum_speed / std::sqrt(speed_squared));
            }
        }
    }
    forces_initialized_ = true;
    force_config_snapshot_ = config_;
    ++metrics_.steps;
    update_metrics();
    return true;
}

const std::vector<Particle>& Engine::particles() const
{
    return particles_;
}

const std::vector<Bond>& Engine::bonds() const
{
    return bonds_;
}

const std::vector<Angle>& Engine::angles() const
{
    return angles_;
}

const Metrics& Engine::metrics() const
{
    return metrics_;
}

Config& Engine::config()
{
    return config_;
}

const Config& Engine::config() const
{
    return config_;
}
}
