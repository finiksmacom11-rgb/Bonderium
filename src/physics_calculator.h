#pragma once

#include <array>
#include <string>

namespace physics_calculator
{
enum class Formula
{
    kinetic_energy,
    momentum,
    acceleration,
    coulomb_potential,
    coulomb_force,
    ideal_gas_pressure
};

bool evaluate(const std::string& expression, double& result,
    std::string& error);
bool evaluate(const std::string& expression, double x, double& result,
    std::string& error);
bool differentiate(const std::string& expression, double x, double step,
    double& result, std::string& error);
bool integrate(const std::string& expression, double start, double end,
    std::size_t intervals, double& result, std::string& error);
bool calculate(Formula formula, const std::array<double, 3>& values,
    double& result, std::string& error);
}
