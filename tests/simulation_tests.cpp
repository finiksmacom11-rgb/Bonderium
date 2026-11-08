#include "simulation.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

void check(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "Simulation test failed on line " << line << ".\n";
        std::abort();
    }
}

#undef assert
#define assert(condition) check((condition), __LINE__)

namespace
{
constexpr double tolerance = 1e-5;

bool near(double left, double right, double epsilon = tolerance)
{
    return std::abs(left - right) <= epsilon;
}

void set_neutral_lj(simulation::Engine& engine, std::size_t index)
{
    assert(engine.set_particle_properties(index, 1.0, 0.0, 1.0, 0.0, false));
}

void test_lennard_jones_equilibrium()
{
    simulation::Engine engine;
    engine.config().cutoff = 2.0;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle(
        {std::pow(2.0, 1.0 / 6.0), 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 0.0, 1.0, 1.0, false));
    assert(engine.set_particle_properties(second, 1.0, 0.0, 1.0, 1.0, false));

    std::string error;
    assert(engine.step(error));
    assert(near(engine.particles()[first].force.x, 0.0));
    assert(near(engine.metrics().potential_energy, -1.0));
}

void test_large_particle_contact_expands_neighbor_search()
{
    simulation::Engine engine;
    engine.config().cutoff = 0.5;
    const double contact = 2.0 * std::pow(2.0, 1.0 / 6.0);
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({contact, 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 0.0, 2.0, 1.0, false));
    assert(engine.set_particle_properties(second, 1.0, 0.0, 2.0, 1.0, false));

    std::string error;
    assert(engine.step(error));
    assert(engine.metrics().neighbor_pairs == 1);
    assert(near(engine.metrics().potential_energy, -1.0));
}

void test_coulomb_force_and_energy()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({-1.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({1.0, 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 1.0, 1.0, 0.0, false));
    assert(engine.set_particle_properties(second, 1.0, 1.0, 1.0, 0.0, false));

    std::string error;
    assert(engine.step(error));
    assert(engine.particles()[first].force.x < 0.0);
    assert(near(engine.metrics().potential_energy, 0.5));
}

void test_coulomb_remains_long_range_and_tracks_charge_changes()
{
    simulation::Engine engine;
    engine.config().cutoff = 1.0;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({2.0, 0.0, 0.0});
    const std::size_t third = engine.add_particle({4.0, 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 1.0, 1.0, 0.0, true));
    assert(engine.set_particle_properties(second, 1.0, 1.0, 1.0, 0.0, true));
    assert(engine.set_particle_properties(third, 1.0, 0.0, 1.0, 0.0, true));

    std::string error;
    assert(engine.step(error));
    assert(near(engine.particles()[first].force.x, -0.25));
    assert(near(engine.metrics().potential_energy, 0.5));

    assert(engine.set_charge(third, 1.0));
    assert(engine.step(error));
    assert(near(engine.particles()[first].force.x, -0.3125));
    assert(near(engine.metrics().potential_energy, 0.5 + 0.25 + 0.5));

    assert(engine.set_particle_properties(third, 1.0, 0.0, 1.0, 0.0, true));
    assert(engine.step(error));
    assert(near(engine.particles()[first].force.x, -0.25));
}

void test_neighbor_cache_and_invalidation()
{
    simulation::Engine engine;
    engine.config().cutoff = 1.0;
    engine.config().neighbor_skin = 0.4;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({2.0, 0.0, 0.0});
    set_neutral_lj(engine, first);
    set_neutral_lj(engine, second);

    std::string error;
    assert(engine.step(error));
    assert(engine.metrics().neighbor_rebuilds == 1);
    assert(engine.metrics().neighbor_pairs == 0);
    assert(engine.set_position(second, {1.1, 0.0, 0.0}));
    assert(engine.step(error));
    assert(engine.metrics().neighbor_rebuilds == 2);
    assert(engine.metrics().neighbor_pairs == 1);

    assert(engine.set_position(second, {1.2, 0.0, 0.0}));
    assert(engine.step(error));
    assert(engine.metrics().neighbor_rebuilds == 2);
    engine.config().cutoff = 0.8;
    assert(engine.step(error));
    assert(engine.metrics().neighbor_rebuilds == 3);
}

void test_harmonic_bond()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({2.0, 0.0, 0.0});
    std::string error;
    assert(engine.add_bond({first, second, 1.0, 10.0}, error));
    assert(engine.step(error));
    assert(engine.particles()[first].velocity.x > 0.0);
    assert(engine.metrics().potential_energy > 0.0);
}

void test_external_force_changes_velocity_without_teleporting()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle = engine.add_particle({1.0, 2.0, 3.0});
    set_neutral_lj(engine, particle);

    std::string error;
    assert(engine.step(error));
    assert(engine.set_external_force(particle, {4.0, 0.0, 0.0}));
    assert(engine.step(error));
    const simulation::Particle& accelerated = engine.particles()[particle];
    assert(near(accelerated.position.x, 1.0002));
    assert(near(accelerated.position.y, 2.0));
    assert(near(accelerated.position.z, 3.0));
    assert(near(accelerated.velocity.x, 0.04));

    assert(engine.set_external_force(particle, {}));
    assert(engine.step(error));
    assert(near(engine.particles()[particle].velocity.x, 0.04));
}

void test_persistent_and_transient_forces_are_independent()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle = engine.add_particle({0.0, 0.0, 0.0});
    assert(engine.set_particle_properties(particle, 1.0, 0.0, 0.5, 0.0, false));
    assert(engine.set_external_force(particle, {1.0, 0.0, 0.0}));
    assert(engine.set_transient_force(particle, {0.0, 2.0, 0.0}));

    std::string error;
    assert(engine.step(error));
    assert(engine.set_transient_force(particle, {}));
    assert(engine.step(error));
    assert(engine.particles()[particle].force.x == 1.0);
    assert(engine.particles()[particle].force.y == 0.0);
    assert(engine.particles()[particle].external_force.x == 1.0);
    assert(engine.particles()[particle].transient_force.x == 0.0);
}

void test_particle_removal_reindexes_bonds_and_angles()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t center = engine.add_particle({1.0, 0.0, 0.0});
    const std::size_t third = engine.add_particle({1.0, 1.0, 0.0});
    const std::size_t isolated = engine.add_particle({4.0, 0.0, 0.0});
    std::string error;
    assert(engine.add_bond({first, center, 1.0, 1.0}, error));
    assert(engine.add_bond({center, third, 1.0, 1.0}, error));
    assert(engine.angles().size() == 1);
    assert(engine.remove_particles({0, 0}));
    assert(engine.particles().size() == 3);
    assert(engine.bonds().size() == 1);
    assert(engine.bonds()[0].first == 0);
    assert(engine.bonds()[0].second == 1);
    assert(engine.angles().empty());
    assert(engine.particles()[2].position.x == 4.0);
    assert(!engine.remove_particles({isolated}));

    assert(engine.add_wall({{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, false},
        error));
    engine.clear_walls();
    assert(engine.walls().empty());
}

void test_overlapping_particles_are_resolved_without_penetration()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({0.5, 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 0.0, 1.0, 0.0, true));
    assert(engine.set_particle_properties(second, 1.0, 0.0, 1.0, 0.0, false));
    assert(engine.set_velocity(second, {-1.0, 0.0, 0.0}));

    std::string error;
    assert(engine.step(error));
    const simulation::Vec3 displacement{
        engine.particles()[1].position.x - engine.particles()[0].position.x,
        engine.particles()[1].position.y - engine.particles()[0].position.y,
        engine.particles()[1].position.z - engine.particles()[0].position.z};
    assert(std::sqrt(displacement.x * displacement.x +
        displacement.y * displacement.y + displacement.z * displacement.z) >=
        std::pow(2.0, 1.0 / 6.0) - tolerance);
    assert(engine.particles()[1].velocity.x > 0.0);
}

void test_freezing_stops_motion_and_releases_applied_force()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle = engine.add_particle({1.0, 0.0, 0.0});
    assert(engine.set_velocity(particle, {2.0, 0.0, 0.0}));
    assert(engine.set_external_force(particle, {4.0, 0.0, 0.0}));
    assert(engine.set_particle_fixed(particle, true));

    std::string error;
    assert(engine.step(error));
    const simulation::Particle& frozen = engine.particles()[particle];
    assert(frozen.fixed);
    assert(near(frozen.position.x, 1.0));
    assert(near(frozen.velocity.x, 0.0));
    assert(near(frozen.external_force.x, 0.0));

    assert(engine.set_particle_fixed(particle, false));
    assert(engine.step(error));
    assert(!engine.particles()[particle].fixed);
    assert(near(engine.particles()[particle].position.x, 1.0));
}

void test_harmonic_angle()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({1.0, 0.0, 0.0});
    const std::size_t center = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t third = engine.add_particle({0.5, 0.866025403784, 0.0});
    std::string error;
    assert(engine.add_angle(
        {first, center, third, 1.5707963267948966, 2.0}, error));
    assert(engine.step(error));
    assert(engine.metrics().potential_energy > 0.0);
    assert(std::isfinite(engine.particles()[first].force.x));
    assert(std::isfinite(engine.particles()[third].force.y));
}

double angle_energy_at(simulation::Engine& engine, std::size_t first,
    double first_x, std::string& error)
{
    assert(engine.set_position(first, {first_x, 0.0, 0.0}));
    assert(engine.step(error));
    return engine.metrics().potential_energy;
}

void test_angle_force_matches_energy_gradient()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({1.0, 0.0, 0.0});
    const std::size_t center = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t third = engine.add_particle({0.5, 0.866025403784, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 0.0, 1.0, 0.0, true));
    assert(engine.set_particle_properties(center, 1.0, 0.0, 1.0, 0.0, true));
    assert(engine.set_particle_properties(third, 1.0, 0.0, 1.0, 0.0, true));
    std::string error;
    assert(engine.add_angle(
        {first, center, third, 1.5707963267948966, 2.0}, error));
    assert(engine.step(error));
    const double force = engine.particles()[first].force.x;
    constexpr double difference_step = 1e-5;
    const double positive_energy = angle_energy_at(
        engine, first, 1.0 + difference_step, error);
    const double negative_energy = angle_energy_at(
        engine, first, 1.0 - difference_step, error);
    const double energy_gradient =
        (positive_energy - negative_energy) / (2.0 * difference_step);
    assert(near(force, -energy_gradient, 1e-4));
}

void test_bond_creation_adds_shared_harmonic_angle()
{
    simulation::Engine engine;
    const std::size_t first = engine.add_particle({1.0, 0.0, 0.0});
    const std::size_t center = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t third = engine.add_particle({0.0, 1.0, 0.0});
    std::string error;
    assert(engine.add_bond({first, center, 1.0, 10.0}, error));
    assert(engine.add_bond({center, third, 1.0, 10.0}, error));
    assert(engine.bonds().size() == 2);
    assert(engine.angles().size() == 1);
    assert(near(engine.angles()[0].equilibrium_radians,
        1.5707963267948966));
}

void test_force_cache_tracks_config_changes()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle = engine.add_particle({0.0, 0.0, 0.0});
    std::string error;
    assert(engine.step(error));
    engine.config().gravity_z = -1.0;
    assert(engine.step(error));
    assert(engine.particles()[particle].velocity.z < 0.0);
}

void test_thermodynamic_metrics_for_confined_gas()
{
    simulation::Engine engine;
    engine.config().time_step = 0.001;
    engine.config().boundary_mode = simulation::BoundaryMode::reflective;
    engine.config().boundary_min = -1.0;
    engine.config().boundary_max = 1.0;
    const std::size_t particle = engine.add_particle({0.0, 0.0, 0.0});
    assert(engine.set_velocity(particle, {2.0, 0.0, 0.0}));

    std::string error;
    assert(engine.step(error));
    const simulation::Metrics& metrics = engine.metrics();
    assert(near(metrics.kinetic_energy, 2.0));
    assert(near(metrics.potential_energy, 0.0));
    assert(near(metrics.total_energy, 2.0));
    assert(near(metrics.temperature, 4.0 / 3.0));
    assert(metrics.pressure_available);
    assert(near(metrics.pressure, 1.0 / 6.0));
    assert(near(metrics.number_density, 1.0 / 8.0));

    engine.config().boundary_mode = simulation::BoundaryMode::none;
    assert(engine.step(error));
    assert(!engine.metrics().pressure_available);
}

void test_virial_pressure_includes_lennard_jones_interactions()
{
    simulation::Engine engine;
    engine.config().cutoff = 2.0;
    engine.config().boundary_mode = simulation::BoundaryMode::reflective;
    engine.config().boundary_min = -5.0;
    engine.config().boundary_max = 5.0;
    const std::size_t first = engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = engine.add_particle({1.0, 0.0, 0.0});
    assert(engine.set_particle_properties(first, 1.0, 0.0, 1.0, 1.0, true));
    assert(engine.set_particle_properties(second, 1.0, 0.0, 1.0, 1.0, true));

    std::string error;
    assert(engine.step(error));
    assert(near(engine.metrics().configurational_virial, 24.0));
    assert(near(engine.metrics().pressure, 24.0 / 3000.0));
}

void test_soft_wall_force_and_overlapping_particle_resolution()
{
    simulation::Engine wall_engine;
    wall_engine.config().boundary_mode = simulation::BoundaryMode::soft;
    wall_engine.config().boundary_min = -1.0;
    wall_engine.config().boundary_max = 1.0;
    wall_engine.config().wall_stiffness = 2.0;
    const std::size_t particle =
        wall_engine.add_particle({1.1, 0.0, 0.0});
    std::string error;
    assert(wall_engine.step(error));
    assert(wall_engine.particles()[particle].force.x < 0.0);
    assert(wall_engine.metrics().potential_energy > 0.0);

    simulation::Engine overlap_engine;
    const std::size_t first = overlap_engine.add_particle({0.0, 0.0, 0.0});
    const std::size_t second = overlap_engine.add_particle({0.0, 0.0, 0.0});
    assert(overlap_engine.set_particle_properties(
        first, 1.0, 0.0, 1.0, 1.0, false));
    assert(overlap_engine.set_particle_properties(
        second, 1.0, 0.0, 1.0, 1.0, false));
    error.clear();
    assert(overlap_engine.step(error));
    assert(error.empty());
    assert(near(overlap_engine.particles()[1].position.x -
        overlap_engine.particles()[0].position.x, std::pow(2.0, 1.0 / 6.0)));
}

void test_reflective_boundary_and_thermostat_modes()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    engine.config().boundary_mode = simulation::BoundaryMode::reflective;
    engine.config().boundary_min = -1.0;
    engine.config().boundary_max = 1.0;
    const std::size_t particle = engine.add_particle({0.9, 0.0, 0.0});
    assert(engine.set_velocity(particle, {20.0, 0.0, 0.0}));

    std::string error;
    assert(engine.step(error));
    assert(near(engine.particles()[particle].position.x,
        1.0 - 0.75 * 0.5612310241546865));
    assert(engine.particles()[particle].velocity.x < 0.0);

    for (const simulation::Thermostat thermostat : {
             simulation::Thermostat::andersen,
             simulation::Thermostat::berendsen,
             simulation::Thermostat::acceleration_damping})
    {
        engine.config().thermostat = thermostat;
        engine.config().thermostat_rate = 1.0;
        engine.config().target_temperature = 0.1;
        assert(engine.step(error));
        assert(std::isfinite(engine.metrics().temperature));
    }
}

void test_barnes_hut_coulomb_path()
{
    simulation::Engine engine;
    engine.config().coulomb_solver = simulation::CoulombSolver::barnes_hut;
    for (int index = 0; index < 64; ++index)
    {
        const std::size_t particle = engine.add_particle({
            static_cast<double>(index % 4),
            static_cast<double>((index / 4) % 4),
            static_cast<double>(index / 16)});
        assert(engine.set_particle_properties(
            particle, 1.0, index % 2 == 0 ? 1.0 : -1.0, 1.0, 0.0, false));
    }

    std::string error;
    assert(engine.step(error));
    assert(std::isfinite(engine.metrics().potential_energy));
    for (const simulation::Particle& particle : engine.particles())
    {
        assert(std::isfinite(particle.force.x));
        assert(std::isfinite(particle.force.y));
        assert(std::isfinite(particle.force.z));
    }
}

void test_dimension_mode_constrains_particles_to_xy()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle =
        engine.add_particle({0.0, 0.0, 3.0});
    assert(engine.set_velocity(particle, {0.0, 0.0, 4.0}));
    assert(engine.set_external_force(particle, {0.0, 0.0, 100.0}));
    assert(engine.set_dimension_mode(true));
    assert(engine.particles()[particle].position.z == 0.0);
    assert(engine.particles()[particle].velocity.z == 0.0);

    std::string error;
    assert(engine.step(error));
    assert(engine.particles()[particle].position.z == 0.0);
    assert(engine.particles()[particle].velocity.z == 0.0);
    assert(near(engine.metrics().temperature, 0.0));
}

void test_two_dimensional_line_wall_reflects_swept_particle()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    assert(engine.set_dimension_mode(true));
    const std::size_t particle =
        engine.add_particle({-1.0, 0.0, 0.0});
    set_neutral_lj(engine, particle);
    assert(engine.set_velocity(particle, {200.0, 0.0, 0.0}));
    std::string error;
    assert(engine.add_wall(
        {{0.0, -2.0, 0.0}, {0.0, 2.0, 0.0}, false}, error));

    assert(engine.step(error));
    assert(engine.particles()[particle].position.x < 0.0);
    assert(engine.particles()[particle].velocity.x < 0.0);
    assert(engine.particles()[particle].position.z == 0.0);
    assert(engine.particles()[particle].velocity.z == 0.0);
}

void test_three_dimensional_plane_wall_reflects_swept_particle()
{
    simulation::Engine engine;
    engine.config().time_step = 0.01;
    const std::size_t particle =
        engine.add_particle({0.0, 0.0, -1.0});
    set_neutral_lj(engine, particle);
    assert(engine.set_velocity(particle, {0.0, 0.0, 200.0}));
    std::string error;
    assert(engine.add_wall(
        {{-2.0, -2.0, 0.0}, {2.0, 2.0, 0.0}, true}, error));

    assert(engine.step(error));
    assert(engine.particles()[particle].position.z < 0.0);
    assert(engine.particles()[particle].velocity.z < 0.0);
}

void test_particle_element_identity()
{
    simulation::Engine engine;
    const std::size_t particle = engine.add_particle({0.0, 0.0, 0.0});
    assert(engine.particles()[particle].atomic_number == 6);
    assert(engine.set_particle_element(particle, 8));
    assert(engine.particles()[particle].atomic_number == 8);
    assert(!engine.set_particle_element(particle, 0));
    assert(!engine.set_particle_element(particle, 119));
    assert(!engine.set_particle_element(particle + 1, 1));
}
}

int main()
{
    test_lennard_jones_equilibrium();
    test_large_particle_contact_expands_neighbor_search();
    test_coulomb_force_and_energy();
    test_coulomb_remains_long_range_and_tracks_charge_changes();
    test_neighbor_cache_and_invalidation();
    test_harmonic_bond();
    test_external_force_changes_velocity_without_teleporting();
    test_persistent_and_transient_forces_are_independent();
    test_particle_removal_reindexes_bonds_and_angles();
    test_overlapping_particles_are_resolved_without_penetration();
    test_freezing_stops_motion_and_releases_applied_force();
    test_harmonic_angle();
    test_angle_force_matches_energy_gradient();
    test_bond_creation_adds_shared_harmonic_angle();
    test_force_cache_tracks_config_changes();
    test_thermodynamic_metrics_for_confined_gas();
    test_virial_pressure_includes_lennard_jones_interactions();
    test_soft_wall_force_and_overlapping_particle_resolution();
    test_reflective_boundary_and_thermostat_modes();
    test_barnes_hut_coulomb_path();
    test_dimension_mode_constrains_particles_to_xy();
    test_two_dimensional_line_wall_reflects_swept_particle();
    test_three_dimensional_plane_wall_reflects_swept_particle();
    test_particle_element_identity();
    std::cout << "Simulation tests passed.\n";
    return 0;
}
