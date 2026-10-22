#pragma once

#include <string>
#include "ParsedPathDoc.h"

namespace rigkit {
namespace svg {

/**
 * @brief Project ParsedPathDoc through a RigWorks 0.40.0 document.
 * @details One entity per path, text run, clip, font-face, and gradient.
 * CSS is not expanded here - only fields the loader already filled.
 */
bool writeDocument(const plot::ParsedPathDoc& doc, std::string& outJson);

/**
 * @brief Read a RigWorks document back into ParsedPathDoc.
 * @details Rebuilds bags from `rig.geometry.path`, `rig.media.text`,
 * `rig.media.text_path`, `rig.paint.clip`, and `rig.media.font_face`.
 */
bool readDocument(const std::string& jsonText, plot::ParsedPathDoc& out);

} // namespace svg
} // namespace rigkit
