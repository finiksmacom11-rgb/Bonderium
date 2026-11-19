#include "simulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <memory>
#include <vector>

namespace
{
constexpr int warmup_steps = 3;
constexpr int measured_steps = 20;

bool run_case(std::size_t particle_count, double spacing,
    bool charged, const char* label)
{
    simulation::Engine engine;
    engine.set_dimension_mode(true);
    engine.config().time_step = 0.005;
    std::string error;
    const std::size_t molecule_count = particle_count / 2;
    const std::size_t columns = static_cast<std::size_t>(
        std::sqrt(static_cast<double>(molecule_count)));
    for (std::size_t index = 0; index < particle_count; ++index)
    {
        const std::size_t molecule = index / 2;
        const double x = static_cast<double>(molecule % columns) * spacing +
            (index % 2 == 0 ? 0.0 : 0.8);
        const double y = static_cast<double>(molecule / columns) * spacing;
        const std::size_t particle = engine.add_particle({x, y, 0.0});
        const double velocity_x = molecule % 2 == 0 ? 5.0 : -5.0;
        const double velocity_y =
            molecule / columns % 2 == 0 ? 5.0 : -5.0;
        const double internal_velocity = index % 2 == 0 ? 0.05 : -0.05;
        if (!engine.set_velocity(particle,
                {velocity_x, velocity_y + internal_velocity, 0.0}))
        {
            std::cerr << "Failed to assign benchmark particle velocity\n";
            return false;
        }
        if (charged && index % 10 == 0 &&
            !engine.set_charge(particle, index % 20 == 0 ? 1.0 : -1.0))
        {
            std::cerr << "Failed to assign benchmark particle charge\n";
            return false;
        }
    }
    for (std::size_t molecule = 0; molecule < molecule_count; ++molecule)
    {
        const simulation::Bond bond{molecule * 2, molecule * 2 + 1,
            0.8, 10.0};
        if (!engine.add_bond(bond, error, false))
        {
            std::cerr << "Failed to bond benchmark molecule: " << error << '\n';
            return false;
        }
    }

    for (int step = 0; step < warmup_steps; ++step)
    {
        if (!engine.step(error))
        {
            std::cerr << "Benchmark case " << label << " with "
                << particle_count << " particles failed: " << error << '\n';
            return false;
        }
    }
    std::vector<double> step_times;
    step_times.reserve(measured_steps);
    for (int step = 0; step < measured_steps; ++step)
    {
        const auto started_at = std::chrono::steady_clock::now();
        if (!engine.step(error))
        {
            std::cerr << "Benchmark case " << label << " with "
                << particle_count << " particles failed: " << error << '\n';
            return false;
        }
        const auto finished_at = std::chrono::steady_clock::now();
        step_times.push_back(
            std::chrono::duration<double, std::milli>(finished_at - started_at)
                .count());
    }
    std::sort(step_times.begin(), step_times.end());
    const double median_ms = step_times[step_times.size() / 2];
    std::vector<double> snapshot_times;
    snapshot_times.reserve(measured_steps);
    for (int sample = 0; sample < measured_steps; ++sample)
    {
        const auto started_at = std::chrono::steady_clock::now();
        auto worker_copy = std::make_unique<simulation::Engine>(engine);
        const auto finished_at = std::chrono::steady_clock::now();
        if (worker_copy->particles().size() != particle_count)
        {
            std::cerr << "Failed to copy benchmark engine state\n";
            return false;
        }
        snapshot_times.push_back(
            std::chrono::duration<double, std::milli>(finished_at - started_at)
                .count());
    }
    std::sort(snapshot_times.begin(), snapshot_times.end());
    const double median_snapshot_ms =
        snapshot_times[snapshot_times.size() / 2];
    const simulation::Engine worker_snapshot(engine);
    std::vector<double> comparison_times;
    comparison_times.reserve(measured_steps);
    for (int sample = 0; sample < measured_steps; ++sample)
    {
        const auto started_at = std::chrono::steady_clock::now();
        const bool same_state = engine.matches_state(worker_snapshot);
        const auto finished_at = std::chrono::steady_clock::now();
        if (!same_state)
        {
            std::cerr << "Engine state comparison failed\n";
            return false;
        }
        comparison_times.push_back(
            std::chrono::duration<double, std::milli>(finished_at - started_at)
                .count());
    }
    std::sort(comparison_times.begin(), comparison_times.end());
    const double median_comparison_ms =
        comparison_times[comparison_times.size() / 2];

    constexpr int revision_checks_per_sample = 10000;
    const std::uint64_t source_revision = engine.state_revision();
    std::vector<double> revision_check_times;
    revision_check_times.reserve(measured_steps);
    for (int sample = 0; sample < measured_steps; ++sample)
    {
        std::uint64_t matching_revisions = 0;
        const auto started_at = std::chrono::steady_clock::now();
        for (int check = 0; check < revision_checks_per_sample; ++check)
        {
            matching_revisions +=
                engine.state_revision() == source_revision &&
                    engine.config() == worker_snapshot.config()
                ? 1 : 0;
        }
        const auto finished_at = std::chrono::steady_clock::now();
        if (matching_revisions != revision_checks_per_sample)
        {
            std::cerr << "Engine revision comparison failed\n";
            return false;
        }
        revision_check_times.push_back(
            std::chrono::duration<double, std::nano>(
                finished_at - started_at).count() /
            revision_checks_per_sample);
    }
    std::sort(revision_check_times.begin(), revision_check_times.end());
    const double median_revision_check_ns =
        revision_check_times[revision_check_times.size() / 2];

    std::cout << std::left << std::setw(12) << label
        << std::right << std::setw(7) << particle_count
        << " particles  " << std::fixed << std::setprecision(3)
        << median_ms << " ms median/step, "
        << median_snapshot_ms << " ms snapshot, "
        << median_comparison_ms << " ms full state compare, "
        << std::setprecision(1) << median_revision_check_ns
        << " ns/revision/config check\n";
    return true;
}
}

int main()
{
    constexpr std::size_t particle_counts[]{1000, 2000, 5000, 10000};
    std::cout << "Bonderium moving bonded-molecule benchmark (20 measured "
            "2D steps per case)\n"
        << "Results depend on CPU, build mode, and system load.\n\n";
    for (const std::size_t particle_count : particle_counts)
    {
        if (!run_case(particle_count, 2.0, false, "sparse") ||
            !run_case(particle_count, 1.7, false, "dense") ||
            !run_case(particle_count, 2.0, true, "charged"))
        {
            return 1;
        }
    }
    return 0;
}
