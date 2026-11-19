#include "project_file.h"

#include <cmath>
#include <exception>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <ostream>
#include <utility>

namespace project_file
{
namespace
{
constexpr std::size_t maximum_project_items = 1'000'000;

template<typename Enum>
int enum_value(Enum value)
{
    return static_cast<int>(value);
}

bool valid_config(const simulation::Config& config)
{
    return std::isfinite(config.time_step) && config.time_step > 0.0 &&
        config.time_step <= 0.02 && std::isfinite(config.cutoff) &&
        config.cutoff > 0.0 && std::isfinite(config.neighbor_skin) &&
        config.neighbor_skin >= 0.0 &&
        std::isfinite(config.cutoff + config.neighbor_skin) &&
        config.cutoff + config.neighbor_skin <= 1e9 &&
        std::isfinite(config.target_temperature) &&
        config.target_temperature >= 0.0 &&
        std::isfinite(config.thermostat_rate) &&
        config.thermostat_rate >= 0.0 &&
        std::isfinite(config.gravity_z) &&
        std::isfinite(config.coulomb_constant) &&
        config.coulomb_constant >= 0.0 &&
        std::isfinite(config.barnes_hut_theta) &&
        config.barnes_hut_theta > 0.0 &&
        std::isfinite(config.wall_stiffness) &&
        config.wall_stiffness >= 0.0 &&
        std::isfinite(config.boundary_min) &&
        std::isfinite(config.boundary_max) &&
        config.boundary_min < config.boundary_max &&
        std::isfinite(config.restitution) &&
        config.restitution >= 0.0 && config.restitution <= 1.0 &&
        std::isfinite(config.maximum_speed) &&
        config.maximum_speed >= 0.0 &&
        config.integrator >= simulation::Integrator::velocity_verlet &&
        config.integrator <= simulation::Integrator::kick_drift_kick &&
        config.thermostat >= simulation::Thermostat::none &&
        config.thermostat <=
            simulation::Thermostat::acceleration_damping &&
        config.boundary_mode >= simulation::BoundaryMode::none &&
        config.boundary_mode <= simulation::BoundaryMode::reflective &&
        config.coulomb_solver >= simulation::CoulombSolver::direct &&
        config.coulomb_solver <= simulation::CoulombSolver::barnes_hut &&
        (config.thermostat != simulation::Thermostat::berendsen ||
            config.thermostat_rate > 0.0);
}

bool read_count(std::istream& input, std::size_t& count)
{
    unsigned long long parsed = 0;
    if (!(input >> parsed) || parsed > maximum_project_items ||
        parsed > std::numeric_limits<std::size_t>::max())
    {
        return false;
    }
    count = static_cast<std::size_t>(parsed);
    return true;
}

bool valid_boolean(int value)
{
    return value == 0 || value == 1;
}
}

bool save(std::ostream& output, const simulation::Engine& engine,
    std::string& error)
{
    error.clear();
    const simulation::Config& config = engine.config();
    if (!valid_config(config) ||
        engine.particles().size() > maximum_project_items ||
        engine.bonds().size() > maximum_project_items ||
        engine.angles().size() > maximum_project_items ||
        engine.walls().size() > maximum_project_items)
    {
        error = "The project contains invalid settings or too many items.";
        return false;
    }

    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "BONDERIUM_PROJECT 1\n";
    output << "CONFIG " << config.time_step << ' ' << config.cutoff << ' '
        << config.neighbor_skin << ' ' << config.target_temperature << ' '
        << config.thermostat_rate << ' ' << config.gravity_z << ' '
        << config.coulomb_constant << ' ' << config.barnes_hut_theta << ' '
        << config.wall_stiffness << ' ' << config.boundary_min << ' '
        << config.boundary_max << ' ' << config.restitution << ' '
        << config.maximum_speed << ' ' << config.two_dimensional << ' '
        << enum_value(config.integrator) << ' '
        << enum_value(config.thermostat) << ' '
        << enum_value(config.boundary_mode) << ' '
        << enum_value(config.coulomb_solver) << '\n';

    output << "PARTICLES " << engine.particles().size() << '\n';
    const auto finite_vector = [](simulation::Vec3 vector) {
        return std::isfinite(vector.x) && std::isfinite(vector.y) &&
            std::isfinite(vector.z);
    };
    for (const simulation::Particle& particle : engine.particles())
    {
        if (!finite_vector(particle.position) ||
            !finite_vector(particle.velocity) ||
            !finite_vector(particle.external_force) ||
            !std::isfinite(particle.mass) || particle.mass <= 0.0 ||
            !std::isfinite(particle.charge) ||
            !std::isfinite(particle.sigma) || particle.sigma <= 0.0 ||
            !std::isfinite(particle.epsilon) || particle.epsilon < 0.0)
        {
            error = "Cannot save a project containing an invalid particle.";
            return false;
        }
        output << "PARTICLE "
            << particle.position.x << ' ' << particle.position.y << ' '
            << particle.position.z << ' ' << particle.velocity.x << ' '
            << particle.velocity.y << ' ' << particle.velocity.z << ' '
            << particle.external_force.x << ' '
            << particle.external_force.y << ' '
            << particle.external_force.z << ' '
            << particle.atomic_number << ' ' << particle.mass << ' '
            << particle.charge << ' ' << particle.sigma << ' '
            << particle.epsilon << ' ' << particle.fixed << '\n';
    }

    output << "BONDS " << engine.bonds().size() << '\n';
    for (const simulation::Bond& bond : engine.bonds())
    {
        output << "BOND " << bond.first << ' ' << bond.second << ' '
            << bond.equilibrium_length << ' ' << bond.stiffness << '\n';
    }
    output << "ANGLES " << engine.angles().size() << '\n';
    for (const simulation::Angle& angle : engine.angles())
    {
        output << "ANGLE " << angle.first << ' ' << angle.center << ' '
            << angle.third << ' ' << angle.equilibrium_radians << ' '
            << angle.stiffness << '\n';
    }
    output << "WALLS " << engine.walls().size() << '\n';
    for (const simulation::Wall& wall : engine.walls())
    {
        output << "WALL " << wall.start.x << ' ' << wall.start.y << ' '
            << wall.start.z << ' ' << wall.end.x << ' ' << wall.end.y << ' '
            << wall.end.z << ' ' << wall.plane << '\n';
    }
    output.flush();
    if (!output)
    {
        error = "Failed while writing the project file.";
        return false;
    }
    return true;
}

bool load(std::istream& input, simulation::Engine& engine, std::string& error)
{
    error.clear();
    try
    {
        input.imbue(std::locale::classic());
        std::string marker;
        int version = 0;
        simulation::Config config;
        int two_dimensional = 0;
        int integrator = 0;
        int thermostat = 0;
        int boundary_mode = 0;
        int coulomb_solver = 0;
        if (!(input >> marker >> version) ||
            marker != "BONDERIUM_PROJECT" || version != 1 ||
            !(input >> marker) || marker != "CONFIG" ||
            !(input >> config.time_step >> config.cutoff >>
                config.neighbor_skin >> config.target_temperature >>
                config.thermostat_rate >> config.gravity_z >>
                config.coulomb_constant >> config.barnes_hut_theta >>
                config.wall_stiffness >> config.boundary_min >>
                config.boundary_max >> config.restitution >>
                config.maximum_speed >> two_dimensional >> integrator >>
                thermostat >> boundary_mode >> coulomb_solver) ||
            !valid_boolean(two_dimensional) ||
            integrator < enum_value(simulation::Integrator::velocity_verlet) ||
            integrator > enum_value(simulation::Integrator::kick_drift_kick) ||
            thermostat < enum_value(simulation::Thermostat::none) ||
            thermostat >
                enum_value(simulation::Thermostat::acceleration_damping) ||
            boundary_mode < enum_value(simulation::BoundaryMode::none) ||
            boundary_mode > enum_value(simulation::BoundaryMode::reflective) ||
            coulomb_solver < enum_value(simulation::CoulombSolver::direct) ||
            coulomb_solver >
                enum_value(simulation::CoulombSolver::barnes_hut))
        {
            error = "The project header or simulation settings are invalid.";
            return false;
        }
        config.two_dimensional = two_dimensional != 0;
        config.integrator = static_cast<simulation::Integrator>(integrator);
        config.thermostat = static_cast<simulation::Thermostat>(thermostat);
        config.boundary_mode =
            static_cast<simulation::BoundaryMode>(boundary_mode);
        config.coulomb_solver =
            static_cast<simulation::CoulombSolver>(coulomb_solver);
        if (!valid_config(config))
        {
            error = "The project contains simulation settings outside valid ranges.";
            return false;
        }

        simulation::Engine loaded;
        loaded.config() = config;
        std::size_t count = 0;
        if (!(input >> marker) || marker != "PARTICLES" ||
            !read_count(input, count))
        {
            error = "The project particle list is invalid.";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            simulation::Particle particle;
            int fixed = 0;
            if (!(input >> marker) || marker != "PARTICLE" ||
                !(input >> particle.position.x >> particle.position.y >>
                    particle.position.z >> particle.velocity.x >>
                    particle.velocity.y >> particle.velocity.z >>
                    particle.external_force.x >> particle.external_force.y >>
                    particle.external_force.z >> particle.atomic_number >>
                    particle.mass >> particle.charge >> particle.sigma >>
                    particle.epsilon >> fixed) ||
                !valid_boolean(fixed))
            {
                error = "A project particle record is invalid.";
                return false;
            }
            if (config.two_dimensional &&
                (particle.position.z != 0.0 ||
                    particle.velocity.z != 0.0 ||
                    particle.external_force.z != 0.0))
            {
                error = "A 2D project particle must remain in the XY plane.";
                return false;
            }
            const std::size_t particle_index =
                loaded.add_particle(particle.position);
            if (!loaded.set_particle_properties(particle_index, particle.mass,
                    particle.charge, particle.sigma, particle.epsilon,
                    fixed != 0) ||
                !loaded.set_particle_element(
                    particle_index, particle.atomic_number) ||
                !loaded.set_velocity(particle_index, particle.velocity) ||
                !loaded.set_external_force(
                    particle_index, particle.external_force))
            {
                error = "A project particle has invalid physical values.";
                return false;
            }
        }

        if (!(input >> marker) || marker != "BONDS" ||
            !read_count(input, count))
        {
            error = "The project bond list is invalid.";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            simulation::Bond bond;
            if (!(input >> marker) || marker != "BOND" ||
                !(input >> bond.first >> bond.second >>
                    bond.equilibrium_length >> bond.stiffness) ||
                !loaded.add_bond(bond, error, false))
            {
                if (error.empty())
                {
                    error = "A project bond record is invalid.";
                }
                return false;
            }
        }

        if (!(input >> marker) || marker != "ANGLES" ||
            !read_count(input, count))
        {
            error = "The project angle list is invalid.";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            simulation::Angle angle;
            if (!(input >> marker) || marker != "ANGLE" ||
                !(input >> angle.first >> angle.center >> angle.third >>
                    angle.equilibrium_radians >> angle.stiffness) ||
                !loaded.add_angle(angle, error))
            {
                if (error.empty())
                {
                    error = "A project angle record is invalid.";
                }
                return false;
            }
        }

        if (!(input >> marker) || marker != "WALLS" ||
            !read_count(input, count))
        {
            error = "The project wall list is invalid.";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            simulation::Wall wall;
            int plane = 0;
            if (!(input >> marker) || marker != "WALL" ||
                !(input >> wall.start.x >> wall.start.y >> wall.start.z >>
                    wall.end.x >> wall.end.y >> wall.end.z >> plane) ||
                !valid_boolean(plane) ||
                !loaded.add_wall(
                    simulation::Wall{wall.start, wall.end, plane != 0},
                    error))
            {
                if (error.empty())
                {
                    error = "A project wall record is invalid.";
                }
                return false;
            }
        }
        if (input >> marker)
        {
            error = "The project file contains unexpected trailing data.";
            return false;
        }
        if (input.bad() || !input.eof())
        {
            error = "The project file could not be read completely.";
            return false;
        }
        if (!loaded.refresh_state(error))
        {
            return false;
        }
        engine = std::move(loaded);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = std::string("Could not load the project: ") + exception.what();
        return false;
    }
}
}
