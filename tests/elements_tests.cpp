#include "elements.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

void check(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "Element test failed on line " << line << ".\n";
        std::abort();
    }
}

#define CHECK(condition) check((condition), __LINE__)

int main()
{
    const auto& table = elements::all();
    bool occupied[9][18]{};
    CHECK(table.size() == 118);
    for (std::size_t index = 0; index < table.size(); ++index)
    {
        const elements::Element& element = table[index];
        CHECK(element.atomic_number == static_cast<int>(index + 1));
        CHECK(element.symbol != nullptr && element.symbol[0] != L'\0');
        CHECK(element.name != nullptr && element.name[0] != L'\0');
        CHECK(std::isfinite(element.atomic_mass) && element.atomic_mass > 0.0);
        CHECK(std::isfinite(element.covalent_radius_pm) &&
            element.covalent_radius_pm > 0.0);
        CHECK(elements::find(element.atomic_number) == &element);
        const elements::Position position =
            elements::table_position(element.atomic_number);
        CHECK(position.row >= 0 && position.row < 9);
        CHECK(position.column >= 0 && position.column < 18);
        CHECK(!occupied[position.row][position.column]);
        occupied[position.row][position.column] = true;
    }
    CHECK(elements::find(1)->symbol[0] == L'H');
    CHECK(elements::find(118)->symbol[0] == L'O' &&
        elements::find(118)->symbol[1] == L'g');
    CHECK(elements::find(0) == nullptr);
    CHECK(elements::find(119) == nullptr);
    CHECK(elements::table_position(0).row == -1);
    CHECK(elements::table_position(1).row == 0 &&
        elements::table_position(1).column == 0);
    CHECK(elements::table_position(2).column == 17);
    CHECK(elements::table_position(13).row == 2 &&
        elements::table_position(13).column == 12);
    CHECK(elements::table_position(57).row == 5 &&
        elements::table_position(57).column == 2);
    CHECK(elements::table_position(58).row == 7 &&
        elements::table_position(58).column == 3);
    CHECK(elements::table_position(72).row == 5 &&
        elements::table_position(72).column == 3);
    CHECK(elements::table_position(90).row == 8 &&
        elements::table_position(90).column == 3);
    CHECK(elements::table_position(118).row == 6 &&
        elements::table_position(118).column == 17);
    CHECK(elements::category_color(elements::Category::noble_gas) != 0);
    std::wcout << L"All periodic-table data checks passed.\n";
}
