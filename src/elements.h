#pragma once

#include <array>
#include <cstddef>

namespace elements
{
enum class Category
{
    alkali_metal,
    alkaline_earth_metal,
    transition_metal,
    post_transition_metal,
    metalloid,
    reactive_nonmetal,
    halogen,
    noble_gas,
    lanthanoid,
    actinoid
};

struct Element
{
    int atomic_number = 0;
    const wchar_t* symbol = L"";
    const wchar_t* name = L"";
    double atomic_mass = 0.0;
    double covalent_radius_pm = 0.0;
    Category category = Category::reactive_nonmetal;
};

struct Position
{
    int row = -1;
    int column = -1;
};

constexpr std::size_t count = 118;
const std::array<Element, count>& all();
const Element* find(int atomic_number);
Position table_position(int atomic_number);
const wchar_t* category_name(Category category);
unsigned int category_color(Category category);
}
