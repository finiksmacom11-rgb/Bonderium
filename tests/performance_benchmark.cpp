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
    std::string error;
    const std::size_t columns = static_cast<std::size_t>(
        std::sqrt(static_cast<double>(particle_count)));
    for (std::size_t index = 0; index < particle_count; ++index)
    {
        const double x = static_cast<double>(index % columns) * spacing;
        const double y = static_cast<double>(index / columns) * spacing;
        const std::size_t particle = engine.add_particle({x, y, 0.0});
        if (charged && index % 10 == 0 &&
            !engine.set_charge(particle, index % 20 == 0 ? 1.0 : -1.0))
        {
            std::cerr << "Failed to assign benchmark particle charge\n";
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
        auto baseline = std::make_unique<simulation::Engine>(engine);
        auto worker_copy =
            std::make_unique<simulation::Engine>(*baseline);
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
    std::cout << std::left << std::setw(12) << label
        << std::right << std::setw(7) << particle_count
        << " particles  " << std::fixed << std::setprecision(3)
        << median_ms << " ms median/step, "
        << median_snapshot_ms << " ms for two state copies\n";
    return true;
}
}

int main()
{
    constexpr std::size_t particle_counts[]{1000, 5000, 10000};
    std::cout << "Bonderium simulation benchmark (20 measured 2D steps and "
            "two-copy snapshots per case)\n"
        << "Results depend on CPU, build mode, and system load.\n\n";
    for (const std::size_t particle_count : particle_counts)
    {
        if (!run_case(particle_count, 2.0, false, "sparse") ||
            !run_case(particle_count, 1.2, false, "dense") ||
            !run_case(particle_count, 2.0, true, "charged"))
        {
            return 1;
        }
    }
    return 0;
}
