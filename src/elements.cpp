#include "elements.h"

namespace elements
{
namespace
{
constexpr std::array<Element, count> periodic_table{{
    {1, L"H", L"Водород", 1.008, 31, Category::reactive_nonmetal},
    {2, L"He", L"Гелий", 4.0026, 28, Category::noble_gas},
    {3, L"Li", L"Литий", 6.94, 128, Category::alkali_metal},
    {4, L"Be", L"Бериллий", 9.0122, 96, Category::alkaline_earth_metal},
    {5, L"B", L"Бор", 10.81, 84, Category::metalloid},
    {6, L"C", L"Углерод", 12.011, 76, Category::reactive_nonmetal},
    {7, L"N", L"Азот", 14.007, 71, Category::reactive_nonmetal},
    {8, L"O", L"Кислород", 15.999, 66, Category::reactive_nonmetal},
    {9, L"F", L"Фтор", 18.998, 57, Category::halogen},
    {10, L"Ne", L"Неон", 20.180, 58, Category::noble_gas},
    {11, L"Na", L"Натрий", 22.990, 166, Category::alkali_metal},
    {12, L"Mg", L"Магний", 24.305, 141, Category::alkaline_earth_metal},
    {13, L"Al", L"Алюминий", 26.982, 121, Category::post_transition_metal},
    {14, L"Si", L"Кремний", 28.085, 111, Category::metalloid},
    {15, L"P", L"Фосфор", 30.974, 107, Category::reactive_nonmetal},
    {16, L"S", L"Сера", 32.06, 105, Category::reactive_nonmetal},
    {17, L"Cl", L"Хлор", 35.45, 102, Category::halogen},
    {18, L"Ar", L"Аргон", 39.948, 106, Category::noble_gas},
    {19, L"K", L"Калий", 39.098, 203, Category::alkali_metal},
    {20, L"Ca", L"Кальций", 40.078, 176, Category::alkaline_earth_metal},
    {21, L"Sc", L"Скандий", 44.956, 170, Category::transition_metal},
    {22, L"Ti", L"Титан", 47.867, 160, Category::transition_metal},
    {23, L"V", L"Ванадий", 50.942, 153, Category::transition_metal},
    {24, L"Cr", L"Хром", 51.996, 139, Category::transition_metal},
    {25, L"Mn", L"Марганец", 54.938, 139, Category::transition_metal},
    {26, L"Fe", L"Железо", 55.845, 132, Category::transition_metal},
    {27, L"Co", L"Кобальт", 58.933, 126, Category::transition_metal},
    {28, L"Ni", L"Никель", 58.693, 124, Category::transition_metal},
    {29, L"Cu", L"Медь", 63.546, 132, Category::transition_metal},
    {30, L"Zn", L"Цинк", 65.38, 122, Category::transition_metal},
    {31, L"Ga", L"Галлий", 69.723, 122, Category::post_transition_metal},
    {32, L"Ge", L"Германий", 72.630, 120, Category::metalloid},
    {33, L"As", L"Мышьяк", 74.922, 119, Category::metalloid},
    {34, L"Se", L"Селен", 78.971, 120, Category::reactive_nonmetal},
    {35, L"Br", L"Бром", 79.904, 120, Category::halogen},
    {36, L"Kr", L"Криптон", 83.798, 116, Category::noble_gas},
    {37, L"Rb", L"Рубидий", 85.468, 220, Category::alkali_metal},
    {38, L"Sr", L"Стронций", 87.62, 195, Category::alkaline_earth_metal},
    {39, L"Y", L"Иттрий", 88.906, 190, Category::transition_metal},
    {40, L"Zr", L"Цирконий", 91.224, 175, Category::transition_metal},
    {41, L"Nb", L"Ниобий", 92.906, 164, Category::transition_metal},
    {42, L"Mo", L"Молибден", 95.95, 154, Category::transition_metal},
    {43, L"Tc", L"Технеций", 98.0, 147, Category::transition_metal},
    {44, L"Ru", L"Рутений", 101.07, 146, Category::transition_metal},
    {45, L"Rh", L"Родий", 102.91, 142, Category::transition_metal},
    {46, L"Pd", L"Палладий", 106.42, 139, Category::transition_metal},
    {47, L"Ag", L"Серебро", 107.87, 145, Category::transition_metal},
    {48, L"Cd", L"Кадмий", 112.41, 144, Category::transition_metal},
    {49, L"In", L"Индий", 114.82, 142, Category::post_transition_metal},
    {50, L"Sn", L"Олово", 118.71, 139, Category::post_transition_metal},
    {51, L"Sb", L"Сурьма", 121.76, 139, Category::metalloid},
    {52, L"Te", L"Теллур", 127.60, 138, Category::metalloid},
    {53, L"I", L"Иод", 126.90, 139, Category::halogen},
    {54, L"Xe", L"Ксенон", 131.29, 140, Category::noble_gas},
    {55, L"Cs", L"Цезий", 132.91, 244, Category::alkali_metal},
    {56, L"Ba", L"Барий", 137.33, 215, Category::alkaline_earth_metal},
    {57, L"La", L"Лантан", 138.91, 207, Category::lanthanoid},
    {58, L"Ce", L"Церий", 140.12, 204, Category::lanthanoid},
    {59, L"Pr", L"Празеодим", 140.91, 203, Category::lanthanoid},
    {60, L"Nd", L"Неодим", 144.24, 201, Category::lanthanoid},
    {61, L"Pm", L"Прометий", 145.0, 199, Category::lanthanoid},
    {62, L"Sm", L"Самарий", 150.36, 198, Category::lanthanoid},
    {63, L"Eu", L"Европий", 151.96, 198, Category::lanthanoid},
    {64, L"Gd", L"Гадолиний", 157.25, 196, Category::lanthanoid},
    {65, L"Tb", L"Тербий", 158.93, 194, Category::lanthanoid},
    {66, L"Dy", L"Диспрозий", 162.50, 192, Category::lanthanoid},
    {67, L"Ho", L"Гольмий", 164.93, 192, Category::lanthanoid},
    {68, L"Er", L"Эрбий", 167.26, 189, Category::lanthanoid},
    {69, L"Tm", L"Тулий", 168.93, 190, Category::lanthanoid},
    {70, L"Yb", L"Иттербий", 173.05, 187, Category::lanthanoid},
    {71, L"Lu", L"Лютеций", 174.97, 187, Category::lanthanoid},
    {72, L"Hf", L"Гафний", 178.49, 175, Category::transition_metal},
    {73, L"Ta", L"Тантал", 180.95, 170, Category::transition_metal},
    {74, L"W", L"Вольфрам", 183.84, 162, Category::transition_metal},
    {75, L"Re", L"Рений", 186.21, 151, Category::transition_metal},
    {76, L"Os", L"Осмий", 190.23, 144, Category::transition_metal},
    {77, L"Ir", L"Иридий", 192.22, 141, Category::transition_metal},
    {78, L"Pt", L"Платина", 195.08, 136, Category::transition_metal},
    {79, L"Au", L"Золото", 196.97, 136, Category::transition_metal},
    {80, L"Hg", L"Ртуть", 200.59, 132, Category::transition_metal},
    {81, L"Tl", L"Таллий", 204.38, 145, Category::post_transition_metal},
    {82, L"Pb", L"Свинец", 207.2, 146, Category::post_transition_metal},
    {83, L"Bi", L"Висмут", 208.98, 148, Category::post_transition_metal},
    {84, L"Po", L"Полоний", 209.0, 140, Category::post_transition_metal},
    {85, L"At", L"Астат", 210.0, 150, Category::halogen},
    {86, L"Rn", L"Радон", 222.0, 150, Category::noble_gas},
    {87, L"Fr", L"Франций", 223.0, 260, Category::alkali_metal},
    {88, L"Ra", L"Радий", 226.0, 221, Category::alkaline_earth_metal},
    {89, L"Ac", L"Актиний", 227.0, 215, Category::actinoid},
    {90, L"Th", L"Торий", 232.04, 206, Category::actinoid},
    {91, L"Pa", L"Протактиний", 231.04, 200, Category::actinoid},
    {92, L"U", L"Уран", 238.03, 196, Category::actinoid},
    {93, L"Np", L"Нептуний", 237.0, 190, Category::actinoid},
    {94, L"Pu", L"Плутоний", 244.0, 187, Category::actinoid},
    {95, L"Am", L"Америций", 243.0, 180, Category::actinoid},
    {96, L"Cm", L"Кюрий", 247.0, 169, Category::actinoid},
    {97, L"Bk", L"Берклий", 247.0, 168, Category::actinoid},
    {98, L"Cf", L"Калифорний", 251.0, 168, Category::actinoid},
    {99, L"Es", L"Эйнштейний", 252.0, 165, Category::actinoid},
    {100, L"Fm", L"Фермий", 257.0, 167, Category::actinoid},
    {101, L"Md", L"Менделевий", 258.0, 173, Category::actinoid},
    {102, L"No", L"Нобелий", 259.0, 176, Category::actinoid},
    {103, L"Lr", L"Лоуренсий", 266.0, 161, Category::actinoid},
    {104, L"Rf", L"Резерфордий", 267.0, 157, Category::transition_metal},
    {105, L"Db", L"Дубний", 268.0, 149, Category::transition_metal},
    {106, L"Sg", L"Сиборгий", 269.0, 143, Category::transition_metal},
    {107, L"Bh", L"Борий", 270.0, 141, Category::transition_metal},
    {108, L"Hs", L"Хассий", 277.0, 134, Category::transition_metal},
    {109, L"Mt", L"Мейтнерий", 278.0, 129, Category::transition_metal},
    {110, L"Ds", L"Дармштадтий", 281.0, 128, Category::transition_metal},
    {111, L"Rg", L"Рентгений", 282.0, 121, Category::transition_metal},
    {112, L"Cn", L"Коперниций", 285.0, 122, Category::transition_metal},
    {113, L"Nh", L"Нихоний", 286.0, 136, Category::post_transition_metal},
    {114, L"Fl", L"Флеровий", 289.0, 143, Category::post_transition_metal},
    {115, L"Mc", L"Московий", 290.0, 162, Category::post_transition_metal},
    {116, L"Lv", L"Ливерморий", 293.0, 175, Category::post_transition_metal},
    {117, L"Ts", L"Теннессин", 294.0, 165, Category::halogen},
    {118, L"Og", L"Оганесон", 294.0, 157, Category::noble_gas}
}};
}

const std::array<Element, count>& all()
{
    return periodic_table;
}

const Element* find(int atomic_number)
{
    if (atomic_number < 1 ||
        atomic_number > static_cast<int>(periodic_table.size()))
    {
        return nullptr;
    }
    return &periodic_table[static_cast<std::size_t>(atomic_number - 1)];
}

Position table_position(int atomic_number)
{
    if (atomic_number == 1) return {0, 0};
    if (atomic_number == 2) return {0, 17};
    if (atomic_number <= 4 && atomic_number >= 3)
        return {1, atomic_number - 3};
    if (atomic_number <= 10 && atomic_number >= 5)
        return {1, atomic_number + 7};
    if (atomic_number <= 12 && atomic_number >= 11)
        return {2, atomic_number - 11};
    if (atomic_number <= 18 && atomic_number >= 13)
        return {2, atomic_number - 1};
    if (atomic_number <= 36 && atomic_number >= 19)
        return {3, atomic_number - 19};
    if (atomic_number <= 54 && atomic_number >= 37)
        return {4, atomic_number - 37};
    if (atomic_number <= 57 && atomic_number >= 55)
        return {5, atomic_number - 55};
    if (atomic_number <= 71 && atomic_number >= 58)
        return {7, atomic_number - 55};
    if (atomic_number <= 86 && atomic_number >= 72)
        return {5, atomic_number - 69};
    if (atomic_number <= 89 && atomic_number >= 87)
        return {6, atomic_number - 87};
    if (atomic_number <= 103 && atomic_number >= 90)
        return {8, atomic_number - 87};
    if (atomic_number <= 118 && atomic_number >= 104)
        return {6, atomic_number - 101};
    return {};
}

const wchar_t* category_name(Category category)
{
    switch (category)
    {
    case Category::alkali_metal: return L"Щелочной металл";
    case Category::alkaline_earth_metal: return L"Щелочноземельный металл";
    case Category::transition_metal: return L"Переходный металл";
    case Category::post_transition_metal: return L"Постпереходный металл";
    case Category::metalloid: return L"Металлоид";
    case Category::reactive_nonmetal: return L"Неметалл";
    case Category::halogen: return L"Галоген";
    case Category::noble_gas: return L"Благородный газ";
    case Category::lanthanoid: return L"Лантаноид";
    case Category::actinoid: return L"Актиноид";
    }
    return L"Элемент";
}

unsigned int category_color(Category category)
{
    switch (category)
    {
    case Category::alkali_metal: return 0xFF8A78;
    case Category::alkaline_earth_metal: return 0xFFD166;
    case Category::transition_metal: return 0x68C4FF;
    case Category::post_transition_metal: return 0x70D6B0;
    case Category::metalloid: return 0xB49AFF;
    case Category::reactive_nonmetal: return 0x83D98F;
    case Category::halogen: return 0xFF9E73;
    case Category::noble_gas: return 0x69DCE0;
    case Category::lanthanoid: return 0xE98BD0;
    case Category::actinoid: return 0xC68AE8;
    }
    return 0xDDE6F2;
}
}
