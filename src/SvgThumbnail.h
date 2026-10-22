#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rigkit {
namespace svg {

/**
 * @brief CPU raster of an SVG for file-browser Icons.
 * @details Loads through SvgLoader, then paints PathFlatten contours and
 * `TextItem` glyphs into an RGBA tile. Long edge is capped at @p maxEdge.
 * Uses Blend2D only when that target is already linked; otherwise a coverage
 * fill. No GL context and no UI pack.
 */
bool rasterizeFileThumbnail(const std::string& pathUtf8, int maxEdge, int& w, int& h,
							std::vector<std::uint8_t>& rgba);

} // namespace svg
} // namespace rigkit
