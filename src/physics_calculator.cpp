#include "physics_calculator.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <cstddef>

namespace physics_calculator
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double coulomb_constant = 8.9875517923e9;
constexpr double gas_constant = 8.31446261815324;

class ExpressionParser
{
public:
    explicit ExpressionParser(const std::string& expression,
        bool has_variable = false, double variable = 0.0)
        : expression_(expression), has_variable_(has_variable),
          variable_(variable)
    {
    }

    bool parse(double& result, std::string& error)
    {
        skip_spaces();
        if (position_ == expression_.size())
        {
            error = "Enter a mathematical expression.";
            return false;
        }
        if (!parse_sum(result) || !is_finite(result))
        {
            if (error_.empty())
            {
                error_ = "The result is outside the supported numeric range.";
            }
            error = error_;
            return false;
        }
        skip_spaces();
        if (position_ != expression_.size())
        {
            error = "Unexpected text at the end of the expression.";
            return false;
        }
        return true;
    }

private:
    bool parse_sum(double& value)
    {
        if (!parse_product(value))
        {
            return false;
        }
        while (true)
        {
            skip_spaces();
            if (!consume('+') && !consume('-'))
            {
                return true;
            }
            const char operation = expression_[position_ - 1];
            double right = 0.0;
            if (!parse_product(right))
            {
                return false;
            }
            value = operation == '+' ? value + right : value - right;
            if (!is_finite(value))
            {
                return fail("The result is outside the supported numeric range.");
            }
        }
    }

    bool parse_product(double& value)
    {
        if (!parse_unary(value))
        {
            return false;
        }
        while (true)
        {
            skip_spaces();
            if (!consume('*') && !consume('/'))
            {
                return true;
            }
            const char operation = expression_[position_ - 1];
            double right = 0.0;
            if (!parse_unary(right))
            {
                return false;
            }
            if (operation == '/' && right == 0.0)
            {
                return fail("Division by zero is undefined.");
            }
            value = operation == '*' ? value * right : value / right;
            if (!is_finite(value))
            {
                return fail("The result is outside the supported numeric range.");
            }
        }
    }

    bool parse_unary(double& value)
    {
        skip_spaces();
        if (consume('+'))
        {
            return parse_unary(value);
        }
        if (consume('-'))
        {
            if (!parse_unary(value))
            {
                return false;
            }
            value = -value;
            return true;
        }
        return parse_power(value);
    }

    bool parse_power(double& value)
    {
        if (!parse_primary(value))
        {
            return false;
        }
        skip_spaces();
        if (consume('^'))
        {
            double exponent = 0.0;
            if (!parse_unary(exponent))
            {
                return false;
            }
            value = std::pow(value, exponent);
            if (!is_finite(value))
            {
                return fail("The power operation is undefined for these values.");
            }
        }
        return true;
    }

    bool parse_primary(double& value)
    {
        skip_spaces();
        if (consume('('))
        {
            if (!parse_sum(value))
            {
                return false;
            }
            skip_spaces();
            if (!consume(')'))
            {
                return fail("A closing parenthesis is missing.");
            }
            return true;
        }
        if (position_ < expression_.size() &&
            (is_digit(expression_[position_]) ||
                expression_[position_] == '.'))
        {
            return parse_number(value);
        }
        if (position_ < expression_.size() &&
            is_identifier_start(expression_[position_]))
        {
            return parse_identifier(value);
        }
        return fail("Expected a number, constant, function, or parenthesis.");
    }

    bool parse_number(double& value)
    {
        const char* begin = expression_.c_str() + position_;
        char* end = nullptr;
        value = std::strtod(begin, &end);
        if (end == begin)
        {
            return fail("Invalid number.");
        }
        position_ += static_cast<std::size_t>(end - begin);
        if (!is_finite(value))
        {
            return fail("Numbers must be finite.");
        }
        return true;
    }

    bool parse_identifier(double& value)
    {
        const std::size_t start = position_++;
        while (position_ < expression_.size() &&
            is_identifier_char(expression_[position_]))
        {
            ++position_;
        }
        const std::string_view name(
            expression_.data() + start, position_ - start);
        if (name == "pi")
        {
            value = pi;
            return true;
        }
        if (name == "e")
        {
            value = std::exp(1.0);
            return true;
        }
        if (name == "c")
        {
            value = 299792458.0;
            return true;
        }
        if (name == "G")
        {
            value = 6.67430e-11;
            return true;
        }
        if (name == "R")
        {
            value = gas_constant;
            return true;
        }
        if (name == "kB")
        {
            value = 1.380649e-23;
            return true;
        }
        if (name == "e_charge")
        {
            value = 1.602176634e-19;
            return true;
        }
        if (name == "amu")
        {
            value = 1.66053906660e-27;
            return true;
        }
        if (name == "m" || name == "s" || name == "kg" ||
            name == "A" || name == "K" || name == "mol" ||
            name == "cd" || name == "N" || name == "J" ||
            name == "Pa" || name == "C")
        {
            value = 1.0;
            return true;
        }
        if (name == "km")
        {
            value = 1e3;
            return true;
        }
        if (name == "cm")
        {
            value = 1e-2;
            return true;
        }
        if (name == "mm")
        {
            value = 1e-3;
            return true;
        }
        if (name == "um")
        {
            value = 1e-6;
            return true;
        }
        if (name == "nm")
        {
            value = 1e-9;
            return true;
        }
        if (name == "gram")
        {
            value = 1e-3;
            return true;
        }
        if (name == "minute")
        {
            value = 60.0;
            return true;
        }
        if (name == "hour")
        {
            value = 3600.0;
            return true;
        }
        if (name == "liter" || name == "L")
        {
            value = 1e-3;
            return true;
        }
        if (name == "bar")
        {
            value = 1e5;
            return true;
        }
        if (name == "atm")
        {
            value = 101325.0;
            return true;
        }
        if (name == "degree")
        {
            value = pi / 180.0;
            return true;
        }
        if (name == "x" && has_variable_)
        {
            value = variable_;
            return true;
        }
        skip_spaces();
        if (!consume('('))
        {
            return fail("Unknown constant or function.");
        }
        double argument = 0.0;
        if (!parse_sum(argument))
        {
            return false;
        }
        skip_spaces();
        if (!consume(')'))
        {
            return fail("A function call is missing a closing parenthesis.");
        }
        if (name == "sin")
        {
            value = std::sin(argument);
        }
        else if (name == "cos")
        {
            value = std::cos(argument);
        }
        else if (name == "tan")
        {
            value = std::tan(argument);
        }
        else if (name == "asin")
        {
            if (argument < -1.0 || argument > 1.0)
            {
                return fail("Inverse sine requires a value from -1 to 1.");
            }
            value = std::asin(argument);
        }
        else if (name == "acos")
        {
            if (argument < -1.0 || argument > 1.0)
            {
                return fail("Inverse cosine requires a value from -1 to 1.");
            }
            value = std::acos(argument);
        }
        else if (name == "atan")
        {
            value = std::atan(argument);
        }
        else if (name == "sinh")
        {
            value = std::sinh(argument);
        }
        else if (name == "cosh")
        {
            value = std::cosh(argument);
        }
        else if (name == "tanh")
        {
            value = std::tanh(argument);
        }
        else if (name == "sqrt")
        {
            if (argument < 0.0)
            {
                return fail("Square root requires a non-negative value.");
            }
            value = std::sqrt(argument);
        }
        else if (name == "abs")
        {
            value = std::abs(argument);
        }
        else if (name == "ln")
        {
            if (argument <= 0.0)
            {
                return fail("Natural logarithm requires a positive value.");
            }
            value = std::log(argument);
        }
        else if (name == "log")
        {
            if (argument <= 0.0)
            {
                return fail("Logarithm requires a positive value.");
            }
            value = std::log10(argument);
        }
        else if (name == "exp")
        {
            value = std::exp(argument);
        }
        else
        {
            return fail("Unknown mathematical function.");
        }
        if (!is_finite(value))
        {
            return fail("The function result is outside the supported range.");
        }
        return true;
    }

    bool consume(char character)
    {
        if (position_ < expression_.size() &&
            expression_[position_] == character)
        {
            ++position_;
            return true;
        }
        return false;
    }

    void skip_spaces()
    {
        while (position_ < expression_.size() &&
            (expression_[position_] == ' ' ||
                expression_[position_] == '\t' ||
                expression_[position_] == '\r' ||
                expression_[position_] == '\n'))
        {
            ++position_;
        }
    }

    bool fail(const char* message)
    {
        if (error_.empty())
        {
            error_ = message;
        }
        return false;
    }

    static bool is_digit(char character)
    {
        return character >= '0' && character <= '9';
    }

    static bool is_identifier_start(char character)
    {
        return (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') || character == '_';
    }

    static bool is_identifier_char(char character)
    {
        return is_identifier_start(character) || is_digit(character);
    }

    static bool is_finite(double value)
    {
        return std::isfinite(value) &&
            std::abs(value) <= std::numeric_limits<double>::max();
    }

    const std::string& expression_;
    bool has_variable_ = false;
    double variable_ = 0.0;
    std::size_t position_ = 0;
    std::string error_;
};
}

bool evaluate(const std::string& expression, double& result,
    std::string& error)
{
    error.clear();
    ExpressionParser parser(expression);
    return parser.parse(result, error);
}

bool evaluate(const std::string& expression, double x, double& result,
    std::string& error)
{
    error.clear();
    if (!std::isfinite(x))
    {
        error = "The variable must be finite.";
        return false;
    }
    ExpressionParser parser(expression, true, x);
    return parser.parse(result, error);
}

bool differentiate(const std::string& expression, double x, double step,
    double& result, std::string& error)
{
    error.clear();
    if (!std::isfinite(x) || !std::isfinite(step) || step <= 0.0)
    {
        error = "The derivative point must be finite and step positive.";
        return false;
    }
    double left = 0.0;
    double right = 0.0;
    if (!evaluate(expression, x - step, left, error) ||
        !evaluate(expression, x + step, right, error))
    {
        return false;
    }
    result = (right - left) / (2.0 * step);
    if (!std::isfinite(result))
    {
        error = "The derivative is outside the supported numeric range.";
        return false;
    }
    return true;
}

bool integrate(const std::string& expression, double start, double end,
    std::size_t intervals, double& result, std::string& error)
{
    error.clear();
    constexpr std::size_t maximum_intervals = 1'000'000;
    if (!std::isfinite(start) || !std::isfinite(end) ||
        intervals < 2 || intervals > maximum_intervals ||
        intervals % 2 != 0)
    {
        error = "Integration requires finite bounds and an even interval count from 2 to 1000000.";
        return false;
    }
    const double width = (end - start) / static_cast<double>(intervals);
    if (!std::isfinite(width))
    {
        error = "The integration interval is outside the supported range.";
        return false;
    }
    long double sum = 0.0L;
    for (std::size_t index = 0; index <= intervals; ++index)
    {
        const double x = index == intervals
            ? end
            : start + static_cast<double>(index) * width;
        double value = 0.0;
        if (!evaluate(expression, x, value, error))
        {
            return false;
        }
        const unsigned int weight = index == 0 || index == intervals
            ? 1U : index % 2 == 0 ? 2U : 4U;
        sum += static_cast<long double>(weight) * value;
    }
    const long double integral =
        sum * static_cast<long double>(width) / 3.0L;
    result = static_cast<double>(integral);
    if (!std::isfinite(result))
    {
        error = "The integral is outside the supported numeric range.";
        return false;
    }
    return true;
}

bool calculate(Formula formula, const std::array<double, 3>& values,
    double& result, std::string& error)
{
    error.clear();
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            error = "Inputs must be finite numbers.";
            return false;
        }
    }
    switch (formula)
    {
    case Formula::kinetic_energy:
        if (values[0] < 0.0)
        {
            error = "Mass must not be negative.";
            return false;
        }
        result = 0.5 * values[0] * values[1] * values[1];
        break;
    case Formula::momentum:
        if (values[0] < 0.0)
        {
            error = "Mass must not be negative.";
            return false;
        }
        result = values[0] * values[1];
        break;
    case Formula::acceleration:
        if (values[1] <= 0.0)
        {
            error = "Mass must be greater than zero.";
            return false;
        }
        result = values[0] / values[1];
        break;
    case Formula::coulomb_potential:
        if (values[2] <= 0.0)
        {
            error = "Distance must be greater than zero.";
            return false;
        }
        result = coulomb_constant * values[0] * values[1] / values[2];
        break;
    case Formula::coulomb_force:
        if (values[2] <= 0.0)
        {
            error = "Distance must be greater than zero.";
            return false;
        }
        result = coulomb_constant * values[0] * values[1] /
            (values[2] * values[2]);
        break;
    case Formula::ideal_gas_pressure:
        if (values[0] < 0.0 || values[1] < 0.0 || values[2] <= 0.0)
        {
            error = "Moles and temperature must be non-negative and volume positive.";
            return false;
        }
        result = values[0] * gas_constant * values[1] / values[2];
        break;
    default:
        error = "Unknown physics formula.";
        return false;
    }
    if (!std::isfinite(result))
    {
        error = "The result is outside the supported numeric range.";
        return false;
    }
    return true;
}
}
