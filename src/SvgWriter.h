#pragma once

#include <string>
#include "ParsedPathDoc.h"

namespace rigkit {
namespace svg {

/**
 * @brief Write ParsedPathDoc to SVG (mm viewBox, solids + gradient defs).
 * @details ArcTo writes SVG A. OkLab/HSV gradients densify to sRGB stops;
 * stop intensity bakes in linear light and clamps to 0-1.
 * @param paperWMm / paperHMm Root width/height and viewBox.
 */
bool writeString(const plot::ParsedPathDoc& doc, float paperWMm, float paperHMm,
				 std::string& outXml);

/** @brief Write SVG file. */
bool writeFile(const plot::ParsedPathDoc& doc, const std::string& path, float paperWMm,
			   float paperHMm);

} // namespace svg
} // namespace rigkit
