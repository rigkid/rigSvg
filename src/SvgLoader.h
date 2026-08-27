#pragma once

#include <string>
#include "ParsedPathDoc.h"

namespace rigkit {
namespace svg {

/**
 * @brief Load SVG into ParsedPathDoc (geometry + solid/gradient paints).
 * @details Supports path (M/L/H/V/C/S/Q/T/A/Z), line/rect/circle/ellipse/poly,
 * nested `g` layers, transforms, viewBox, fill/stroke colours, stroke-width,
 * and `<linearGradient>` / `<radialGradient>` via `url(#id)`. Arcs stay ArcTo.
 */
bool loadFile(const std::string& path, plot::ParsedPathDoc& out);

/** @brief Parse SVG XML text (same subset as loadFile). */
bool loadString(const std::string& svgText, plot::ParsedPathDoc& out);

} // namespace svg
} // namespace rigkit
