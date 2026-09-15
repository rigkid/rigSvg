#include "SvgThumbnail.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace rigkit {
namespace svg {

bool rasterizeFileThumbnail(const std::string& pathUtf8, int maxEdge, int& w, int& h,
							std::vector<std::uint8_t>& rgba) {
	w = 0;
	h = 0;
	rgba.clear();
	if (pathUtf8.empty() || maxEdge < 1) {
		return false;
	}

	const std::filesystem::path path(std::u8string(pathUtf8.begin(), pathUtf8.end()));
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return false;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff len = in.tellg();
	if (len <= 0 || len > 4 * 1024 * 1024) {
		return false;
	}
	in.seekg(0, std::ios::beg);
	std::vector<char> xml(static_cast<size_t>(len) + 1);
	if (!in.read(xml.data(), len)) {
		return false;
	}
	xml[static_cast<size_t>(len)] = '\0';

	NSVGimage* image = nsvgParse(xml.data(), "px", 96.0f);
	if (!image) {
		return false;
	}

	float srcW = image->width;
	float srcH = image->height;
	if (srcW < 1.f) {
		srcW = 128.f;
	}
	if (srcH < 1.f) {
		srcH = 128.f;
	}
	const float edge = std::max(srcW, srcH);
	const float scale = (edge > static_cast<float>(maxEdge)) ? (static_cast<float>(maxEdge) / edge)
															 : 1.0f;
	const int dw = std::max(1, static_cast<int>(srcW * scale + 0.5f));
	const int dh = std::max(1, static_cast<int>(srcH * scale + 0.5f));

	NSVGrasterizer* rast = nsvgCreateRasterizer();
	if (!rast) {
		nsvgDelete(image);
		return false;
	}

	std::vector<std::uint8_t> pixels(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4);
	nsvgRasterize(rast, image, 0.0f, 0.0f, scale, pixels.data(), dw, dh, dw * 4);
	nsvgDeleteRasterizer(rast);
	nsvgDelete(image);

	w = dw;
	h = dh;
	rgba = std::move(pixels);
	return true;
}

} // namespace svg
} // namespace rigkit
