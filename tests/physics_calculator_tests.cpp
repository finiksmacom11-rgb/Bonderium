#include "physics_calculator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void check(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "Physics calculator test failed on line "
            << line << ".\n";
        std::abort();
    }
}

#undef assert
#define assert(condition) check((condition), __LINE__)

bool near(double actual, double expected, double tolerance = 1e-10)
{
    return std::abs(actual - expected) <= tolerance;
}

void test_math_expression_evaluation()
{
    double result = 0.0;
    std::string error;
    assert(physics_calculator::evaluate(
        "sin(pi/2) + sqrt(9) + log(100)", result, error));
    assert(near(result, 6.0));
    assert(physics_calculator::evaluate(
        "asin(1) + acos(1) + atan(0)", result, error));
    assert(near(result, 1.5707963267948966));
    assert(physics_calculator::evaluate("-2^2 + 2^3^2", result, error));
    assert(near(result, 508.0));
    assert(physics_calculator::evaluate("kB * 300 / e_charge", result, error));
    assert(near(result, 0.025851999786435, 1e-12));
    assert(physics_calculator::evaluate("2 * km / hour", result, error));
    assert(near(result, 0.5555555555555556, 1e-12));
    assert(physics_calculator::evaluate("3 * liter / minute",
        result, error));
    assert(near(result, 5e-5, 1e-12));
}

void test_math_expression_errors()
{
    double result = 0.0;
    std::string error;
    assert(!physics_calculator::evaluate("1 / 0", result, error));
    assert(!error.empty());
    assert(!physics_calculator::evaluate("sqrt(-1)", result, error));
    assert(!error.empty());
    assert(!physics_calculator::evaluate("unknown(2)", result, error));
    assert(!error.empty());
    assert(!physics_calculator::evaluate("2 + (3", result, error));
    assert(!error.empty());
    assert(!physics_calculator::evaluate("1e9999", result, error));
    assert(!error.empty());
    assert(!physics_calculator::evaluate("2(3)", result, error));
    assert(!error.empty());
}

void test_numerical_calculus()
{
    double result = 0.0;
    std::string error;
    assert(physics_calculator::differentiate("x^2", 3.0, 0.001,
        result, error));
    assert(near(result, 6.0, 1e-8));
    assert(physics_calculator::integrate("sin(x)", 0.0,
        3.14159265358979323846, 1000, result, error));
    assert(near(result, 2.0, 1e-10));
    assert(physics_calculator::integrate("x", 2.0, 0.0, 100,
        result, error));
    assert(near(result, -2.0, 1e-10));
    assert(!physics_calculator::integrate("x", 0.0, 1.0, 99,
        result, error));
    assert(!error.empty());
}

void test_physics_formulas_and_domains()
{
    double result = 0.0;
    std::string error;
    assert(physics_calculator::calculate(
        physics_calculator::Formula::kinetic_energy,
        {2.0, 3.0, 0.0}, result, error));
    assert(near(result, 9.0));
    assert(physics_calculator::calculate(
        physics_calculator::Formula::acceleration,
        {10.0, 2.0, 0.0}, result, error));
    assert(near(result, 5.0));
    assert(physics_calculator::calculate(
        physics_calculator::Formula::coulomb_potential,
        {1e-6, -1e-6, 1.0}, result, error));
    assert(near(result, -8.9875517923e-3, 1e-14));
    assert(physics_calculator::calculate(
        physics_calculator::Formula::ideal_gas_pressure,
        {1.0, 273.15, 1.0}, result, error));
    assert(near(result, 2271.0954641485573, 1e-8));
    assert(!physics_calculator::calculate(
        physics_calculator::Formula::acceleration,
        {1.0, 0.0, 0.0}, result, error));
    assert(!error.empty());
}
}

int main()
{
    test_math_expression_evaluation();
    test_math_expression_errors();
    test_numerical_calculus();
    test_physics_formulas_and_domains();
    std::cout << "Physics calculator tests passed.\n";
}
