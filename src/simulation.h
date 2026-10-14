#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace simulation
{
struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Particle
{
    Vec3 position;
    Vec3 velocity;
    Vec3 force;
    Vec3 external_force;
    double mass = 1.0;
    double charge = 0.0;
    double sigma = 0.75;
    double epsilon = 0.1;
    bool fixed = false;
};

struct Bond
{
    std::size_t first = 0;
    std::size_t second = 0;
    double equilibrium_length = 1.0;
    double stiffness = 10.0;
};

struct Angle
{
    std::size_t first = 0;
    std::size_t center = 0;
    std::size_t third = 0;
    double equilibrium_radians = 1.5707963267948966;
    double stiffness = 2.0;
};

enum class Integrator
{
    velocity_verlet,
    kick_drift_kick
};

enum class Thermostat
{
    none,
    andersen,
    berendsen,
    acceleration_damping
};

enum class BoundaryMode
{
    none,
    soft,
    reflective
};

enum class CoulombSolver
{
    direct,
    barnes_hut
};

struct Config
{
    double time_step = 0.001;
    double cutoff = 3.0;
    double neighbor_skin = 0.5;
    double target_temperature = 0.25;
    double thermostat_rate = 1.0;
    double gravity_z = 0.0;
    double coulomb_constant = 1.0;
    double barnes_hut_theta = 0.7;
    double wall_stiffness = 10.0;
    double boundary_min = -20.0;
    double boundary_max = 20.0;
    double restitution = 0.8;
    double maximum_speed = 0.0;
    Integrator integrator = Integrator::velocity_verlet;
    Thermostat thermostat = Thermostat::none;
    BoundaryMode boundary_mode = BoundaryMode::none;
    CoulombSolver coulomb_solver = CoulombSolver::direct;
};

struct Metrics
{
    std::uint64_t steps = 0;
    std::uint64_t neighbor_rebuilds = 0;
    std::size_t neighbor_pairs = 0;
    double kinetic_energy = 0.0;
    double potential_energy = 0.0;
    double total_energy = 0.0;
    double temperature = 0.0;
    double configurational_virial = 0.0;
    double pressure = 0.0;
    double number_density = 0.0;
    bool pressure_available = false;
};

class Engine
{
public:
    std::size_t add_particle(Vec3 position);
    bool add_bond(Bond bond, std::string& error);
    bool add_angle(Angle angle, std::string& error);
    bool set_position(std::size_t index, Vec3 position);
    bool set_velocity(std::size_t index, Vec3 velocity);
    bool set_external_force(std::size_t index, Vec3 force);
    bool set_charge(std::size_t index, double charge);
    bool set_particle_properties(std::size_t index, double mass, double charge,
        double sigma, double epsilon, bool fixed);
    void clear();
    void invalidate_neighbors();
    void invalidate_forces();

    bool step(std::string& error);
    const std::vector<Particle>& particles() const;
    const std::vector<Bond>& bonds() const;
    const std::vector<Angle>& angles() const;
    const Metrics& metrics() const;
    Config& config();
    const Config& config() const;

private:
    struct NeighborPair
    {
        std::size_t first = 0;
        std::size_t second = 0;
        double sigma = 0.0;
        double epsilon = 0.0;
    };

    bool validate_config(std::string& error) const;
    bool force_configuration_matches() const;
    bool compute_forces(std::string& error);
    void rebuild_neighbor_list();
    bool needs_neighbor_rebuild() const;
    void apply_boundaries();
    void apply_thermostat();
    void update_metrics();

    std::vector<Particle> particles_;
    std::vector<Bond> bonds_;
    std::vector<Angle> angles_;
    std::vector<NeighborPair> neighbor_pairs_;
    std::vector<Vec3> neighbor_reference_positions_;
    Config config_;
    Config force_config_snapshot_;
    Metrics metrics_;
    double neighbor_reference_cutoff_ = -1.0;
    double neighbor_reference_skin_ = -1.0;
    bool forces_initialized_ = false;
};
}
