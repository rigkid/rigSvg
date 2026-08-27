#pragma once

#include <string>
#include "CPath.h"

namespace rigkit {
namespace svg {

/**
 * @brief Parse an SVG path `d` attribute into a CPath.
 * @details Supports M/L/H/V/C/S/Q/T/A/Z (absolute + relative). Arcs stay
 * ArcTo (SVG A). Smooth S/T reflect the previous control.
 */
bool parsePathD(const std::string& d, ecs::CPath& out);

} // namespace svg
} // namespace rigkit
