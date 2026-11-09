#include "project_file.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

void check(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "Project file test failed on line " << line << ".\n";
        std::abort();
    }
}

#undef assert
#define assert(condition) check((condition), __LINE__)

namespace
{
void test_project_round_trip()
{
    simulation::Engine source;
    simulation::Config& config = source.config();
    config.time_step = 0.002;
    config.cutoff = 2.5;
    config.neighbor_skin = 0.3;
    config.target_temperature = 0.75;
    config.thermostat_rate = 0.4;
    config.gravity_z = -0.02;
    config.coulomb_constant = 1.7;
    config.barnes_hut_theta = 0.6;
    config.wall_stiffness = 8.0;
    config.boundary_min = -12.0;
    config.boundary_max = 12.0;
    config.restitution = 0.65;
    config.maximum_speed = 15.0;
    config.integrator = simulation::Integrator::kick_drift_kick;
    config.thermostat = simulation::Thermostat::andersen;
    config.boundary_mode = simulation::BoundaryMode::reflective;
    config.coulomb_solver = simulation::CoulombSolver::direct;

    const std::size_t first = source.add_particle({-1.0, 0.0, 0.0});
    const std::size_t center = source.add_particle({0.0, 0.0, 0.0});
    const std::size_t third = source.add_particle({0.0, 1.0, 0.0});
    assert(source.set_particle_properties(first, 1.5, 1.0, 0.8, 0.2, false));
    assert(source.set_particle_element(first, 8));
    assert(source.set_velocity(first, {0.1, 0.2, 0.3}));
    assert(source.set_external_force(first, {0.4, 0.5, 0.6}));
    assert(source.set_transient_force(first, {9.0, 9.0, 9.0}));
    std::string error;
    assert(source.add_bond({first, center, 1.1, 7.0}, error, false));
    assert(source.add_bond({center, third, 1.2, 8.0}, error, false));
    assert(source.add_angle({first, center, third, 1.3, 4.0}, error));
    assert(source.add_wall(
        {{-2.0, -2.0, 0.0}, {2.0, 2.0, 0.0}, true}, error));
    assert(source.refresh_state(error));

    std::stringstream file;
    assert(project_file::save(file, source, error));
    simulation::Engine loaded;
    assert(project_file::load(file, loaded, error));
    simulation::Engine expected = source;
    assert(expected.set_transient_force(first, {}));
    assert(expected.refresh_state(error));
    assert(expected.matches_state(loaded));
    assert(loaded.particles()[first].transient_force.x == 0.0);
    assert(loaded.metrics().kinetic_energy > 0.0);
}

void test_invalid_project_does_not_modify_engine()
{
    simulation::Engine original;
    const std::size_t particle = original.add_particle({1.0, 2.0, 3.0});
    assert(original.set_velocity(particle, {4.0, 5.0, 6.0}));
    const simulation::Engine baseline = original;
    std::stringstream invalid(
        "BONDERIUM_PROJECT 1\n"
        "CONFIG 0.001 3 0.5 0.25 1 0 1 0.7 10 -20 20 0.8 0 0 0 0 0 1\n"
        "PARTICLES 1\n"
        "PARTICLE nan 0 0 0 0 0 0 0 0 6 1 0 0.75 0.1 0\n");
    std::string error;
    assert(!project_file::load(invalid, original, error));
    assert(!error.empty());
    assert(original.matches_state(baseline));
}

void test_rejects_trailing_data()
{
    std::stringstream invalid(
        "BONDERIUM_PROJECT 1\n"
        "CONFIG 0.001 3 0.5 0.25 1 0 1 0.7 10 -20 20 0.8 0 0 0 0 0 1\n"
        "PARTICLES 0\nBONDS 0\nANGLES 0\nWALLS 0\nunexpected\n");
    simulation::Engine loaded;
    std::string error;
    assert(!project_file::load(invalid, loaded, error));
    assert(!error.empty());
    assert(loaded.particles().empty());
}
}

int main()
{
    test_project_round_trip();
    test_invalid_project_does_not_modify_engine();
    test_rejects_trailing_data();
    std::cout << "Project file tests passed.\n";
}
