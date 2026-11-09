#pragma once

#include "simulation.h"

#include <iosfwd>
#include <string>

namespace project_file
{
bool save(std::ostream& output, const simulation::Engine& engine,
    std::string& error);
bool load(std::istream& input, simulation::Engine& engine,
    std::string& error);
}
