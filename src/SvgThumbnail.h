#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rigkit {
namespace svg {

/**
 * @brief CPU raster of an SVG for file-browser Icons.
 * @details Long edge is capped at @p maxEdge. False when the file is missing,
 * too large, or NanoSVG cannot parse it. Not the document import path.
 */
bool rasterizeFileThumbnail(const std::string& pathUtf8, int maxEdge, int& w, int& h,
							std::vector<std::uint8_t>& rgba);

} // namespace svg
} // namespace rigkit
