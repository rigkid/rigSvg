#include "SvgThumbnail.h"

#include "Face.h"
#include "PathBounds.h"
#include "PathFlatten.h"
#include "SvgLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef RIGSVG_HAS_BLEND2D
#include <blend2d.h>
#endif

namespace rigkit {
namespace svg {
namespace {

struct Tile {
	int w = 0;
	int h = 0;
	float ox = 0.f;
	float oy = 0.f;
	float scale = 1.f;
	std::vector<std::uint8_t> rgba;
};

void putPixel(Tile& tile, int x, int y, const glm::vec4& c) {
	if (x < 0 || y < 0 || x >= tile.w || y >= tile.h || c.a <= 0.f) {
		return;
	}
	const size_t i =
		(static_cast<size_t>(y) * static_cast<size_t>(tile.w) + static_cast<size_t>(x)) * 4;
	const float sa = std::clamp(c.a, 0.f, 1.f);
	const float da = tile.rgba[i + 3] / 255.f;
	const float outA = sa + da * (1.f - sa);
	auto mix = [&](float src, std::uint8_t dst) {
		const float d = dst / 255.f;
		const float v = (src * sa + d * da * (1.f - sa)) / std::max(outA, 1e-6f);
		return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
	};
	tile.rgba[i + 0] = mix(c.r, tile.rgba[i + 0]);
	tile.rgba[i + 1] = mix(c.g, tile.rgba[i + 1]);
	tile.rgba[i + 2] = mix(c.b, tile.rgba[i + 2]);
	tile.rgba[i + 3] = static_cast<std::uint8_t>(std::lround(std::clamp(outA, 0.f, 1.f) * 255.f));
}

int windingAt(const std::vector<glm::vec2>& poly, float x, float y) {
	int w = 0;
	const size_t n = poly.size();
	if (n < 2) {
		return 0;
	}
	for (size_t i = 0; i < n; ++i) {
		const glm::vec2& a = poly[i];
		const glm::vec2& b = poly[(i + 1) % n];
		if ((a.y <= y) == (b.y <= y)) {
			continue;
		}
		const float t = (y - a.y) / (b.y - a.y);
		const float xi = a.x + t * (b.x - a.x);
		if (xi > x) {
			w += (b.y > a.y) ? 1 : -1;
		}
	}
	return w;
}

bool hitContours(const std::vector<std::vector<glm::vec2>>& contours, float x, float y,
				 bool evenOdd) {
	int w = 0;
	for (const auto& c : contours) {
		w += windingAt(c, x, y);
	}
	return evenOdd ? ((w & 1) != 0) : (w != 0);
}

float distSeg(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) {
	const glm::vec2 d = b - a;
	const float len2 = glm::dot(d, d);
	if (len2 <= 1e-12f) {
		return glm::length(p - a);
	}
	const float t = std::clamp(glm::dot(p - a, d) / len2, 0.f, 1.f);
	return glm::length(p - (a + t * d));
}

bool hitStroke(const std::vector<std::vector<glm::vec2>>& contours, float x, float y, float half) {
	const glm::vec2 p{x, y};
	for (const auto& c : contours) {
		if (c.size() < 2) {
			continue;
		}
		for (size_t i = 1; i < c.size(); ++i) {
			if (distSeg(c[i - 1], c[i], p) <= half) {
				return true;
			}
		}
	}
	return false;
}

glm::vec4 sampleGradient(const ecs::CGradient& g, float t) {
	if (g.stops.empty()) {
		return {0.f, 0.f, 0.f, 1.f};
	}
	if (g.spread == ecs::CGradient::Spread::Repeat) {
		t = t - std::floor(t);
	} else if (g.spread == ecs::CGradient::Spread::Mirror) {
		t = std::abs(t - 2.f * std::floor(t * 0.5f + 0.5f));
	} else {
		t = std::clamp(t, 0.f, 1.f);
	}
	if (g.stops.size() == 1 || t <= g.stops.front().t) {
		return g.stops.front().rgba;
	}
	for (size_t i = 1; i < g.stops.size(); ++i) {
		if (t <= g.stops[i].t) {
			const float span = std::max(1e-6f, g.stops[i].t - g.stops[i - 1].t);
			const float u = (t - g.stops[i - 1].t) / span;
			return g.stops[i - 1].rgba * (1.f - u) + g.stops[i].rgba * u;
		}
	}
	return g.stops.back().rgba;
}

glm::vec4 paintAt(const plot::ParsedPathDoc& doc, int32_t paint, const glm::vec4& solid,
				  const plot::PathBounds& b, float x, float y) {
	if (paint < 0 || paint >= static_cast<int32_t>(doc.paints.gradients.size())) {
		return solid;
	}
	const auto& g = doc.paints.gradients[static_cast<size_t>(paint)];
	const float w = std::max(1e-6f, b.maxX - b.minX);
	const float h = std::max(1e-6f, b.maxY - b.minY);
	float t = 0.f;
	if (g.kind == ecs::CGradient::Kind::Radial) {
		const float cx = b.minX + g.center.x * w;
		const float cy = b.minY + g.center.y * h;
		const float r = std::max(1e-6f, g.outerRadius * std::max(w, h));
		t = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
	} else {
		t = ((x - b.minX) / w + (y - b.minY) / h) * 0.5f;
	}
	return sampleGradient(g, t);
}

const plot::ParsedPathDoc::Clip* findClip(const plot::ParsedPathDoc& doc, const std::string& id) {
	if (id.empty()) {
		return nullptr;
	}
	for (const auto& c : doc.clips) {
		if (c.id == id) {
			return &c;
		}
	}
	return nullptr;
}

void stampGlyph(Tile& tile, int x0, int y0, const typeface::GlyphCover& cover,
				const glm::vec4& color) {
	for (int y = 0; y < cover.h; ++y) {
		for (int x = 0; x < cover.w; ++x) {
			const float a = cover.a[static_cast<size_t>(y * cover.w + x)] / 255.f;
			if (a <= 0.f) {
				continue;
			}
			putPixel(tile, x0 + x, y0 + y, {color.r, color.g, color.b, color.a * a});
		}
	}
}

void drawFallbackText(Tile& tile, const ecs::TextItem& item) {
	// 5x7 block letters so a missing face still leaves ink on a text tile.
	const float sx = item.baseline.x * tile.scale + tile.ox;
	const float sy = item.baseline.y * tile.scale + tile.oy;
	const int cell = std::max(3, static_cast<int>(item.size * tile.scale * 0.6f));
	int x = static_cast<int>(std::lround(sx));
	if (item.anchor == ecs::TextItem::Anchor::Middle) {
		x -= static_cast<int>(item.text.size()) * (cell + 1) / 2;
	} else if (item.anchor == ecs::TextItem::Anchor::End) {
		x -= static_cast<int>(item.text.size()) * (cell + 1);
	}
	const int y = static_cast<int>(std::lround(sy)) - cell;
	for (size_t i = 0; i < item.text.size(); ++i) {
		if (item.text[i] == ' ') {
			x += cell;
			continue;
		}
		for (int gy = 0; gy < cell; ++gy) {
			for (int gx = 0; gx < std::max(2, cell - 1); ++gx) {
				putPixel(tile, x + gx, y + gy, item.rgba);
			}
		}
		x += cell + 1;
	}
}

void drawText(Tile& tile, const ecs::TextItem& item, const typeface::Face* face) {
	if (item.text.empty()) {
		return;
	}
	if (!face) {
		drawFallbackText(tile, item);
		return;
	}
	const float px = std::max(1.f, item.size * tile.scale);
	float x = item.baseline.x * tile.scale + tile.ox;
	const float y = item.baseline.y * tile.scale + tile.oy;
	auto runWidth = [&](const std::string& s) {
		float width = 0.f;
		int prev = 0;
		const char* p = s.c_str();
		const char* end = p + s.size();
		while (p < end) {
			const int code = typeface::nextUtf8(p, end);
			if (code <= 0) {
				break;
			}
			if (prev) {
				width += typeface::kernCodepoints(*face, prev, code, px);
			}
			width += typeface::advanceCodepoint(*face, code, px);
			prev = code;
		}
		return width;
	};
	if (item.anchor == ecs::TextItem::Anchor::Middle) {
		x -= runWidth(item.text) * 0.5f;
	} else if (item.anchor == ecs::TextItem::Anchor::End) {
		x -= runWidth(item.text);
	}
	int prev = 0;
	const char* p = item.text.c_str();
	const char* end = p + item.text.size();
	while (p < end) {
		const int code = typeface::nextUtf8(p, end);
		if (code <= 0) {
			break;
		}
		if (prev) {
			x += typeface::kernCodepoints(*face, prev, code, px);
		}
		typeface::GlyphCover cover;
		if (typeface::stampCodepoint(*face, code, px, cover) && !cover.a.empty()) {
			stampGlyph(tile, static_cast<int>(std::lround(x)) + cover.xoff,
					   static_cast<int>(std::lround(y)) + cover.yoff, cover, item.rgba);
		}
		x += typeface::advanceCodepoint(*face, code, px);
		prev = code;
	}
}

void drawTexts(Tile& tile, const plot::ParsedPathDoc& doc) {
	typeface::Face face;
	std::string faceKey;
	bool faceTried = false;
	bool haveFace = false;
	for (const auto& layer : doc.layers) {
		for (const auto& t : layer.texts.items) {
			if (!faceTried || t.fontPath != faceKey) {
				haveFace = typeface::loadFace(t.fontPath, face);
				faceKey = t.fontPath;
				faceTried = true;
			}
			drawText(tile, t, haveFace ? &face : nullptr);
		}
	}
}

void fillPath(Tile& tile, const plot::ParsedPathDoc& doc, const ecs::CPaths& bag, size_t i) {
	const auto contours = plot::flattenPathContours(bag.paths[i], 0.35f);
	if (contours.empty()) {
		return;
	}
	const plot::PathBounds b = plot::boundsOfPath(bag.paths[i], 0.35f);
	const bool evenOdd = bag.isEvenOdd(i);
	const std::string clipId = i < bag.pathClipId.size() ? bag.pathClipId[i] : std::string();
	std::vector<std::vector<glm::vec2>> clipContours;
	if (const auto* clip = findClip(doc, clipId)) {
		clipContours = plot::flattenPathContours(clip->path, 0.35f);
	}
	const int minX = std::max(0, static_cast<int>(std::floor(b.minX * tile.scale + tile.ox)) - 2);
	const int minY = std::max(0, static_cast<int>(std::floor(b.minY * tile.scale + tile.oy)) - 2);
	const int maxX =
		std::min(tile.w - 1, static_cast<int>(std::ceil(b.maxX * tile.scale + tile.ox)) + 2);
	const int maxY =
		std::min(tile.h - 1, static_cast<int>(std::ceil(b.maxY * tile.scale + tile.oy)) + 2);
	const bool filled = bag.isFilled(i);
	const bool stroked = bag.isStroked(i);
	const float half = bag.strokeWidth(i, 1.f) * tile.scale * 0.5f;
	for (int y = minY; y <= maxY; ++y) {
		for (int x = minX; x <= maxX; ++x) {
			const float px = (static_cast<float>(x) + 0.5f - tile.ox) / tile.scale;
			const float py = (static_cast<float>(y) + 0.5f - tile.oy) / tile.scale;
			if (!clipContours.empty() && !hitContours(clipContours, px, py, false)) {
				continue;
			}
			if (filled && hitContours(contours, px, py, evenOdd)) {
				const glm::vec4 solid =
					i < bag.pathColors.size() ? bag.pathColors[i] : glm::vec4{0, 0, 0, 1};
				putPixel(tile, x, y, paintAt(doc, bag.fillPaint(i), solid, b, px, py));
			}
			if (stroked && half > 0.f && hitStroke(contours, px, py, half / tile.scale)) {
				const glm::vec4 solid = i < bag.pathStrokeColors.size() ? bag.pathStrokeColors[i]
																		: glm::vec4{0, 0, 0, 1};
				putPixel(tile, x, y, paintAt(doc, bag.strokePaint(i), solid, b, px, py));
			}
		}
	}
}

#ifdef RIGSVG_HAS_BLEND2D
bool rasterBlend2D(const plot::ParsedPathDoc& doc, Tile& tile) {
	BLImage img;
	if (img.create(tile.w, tile.h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
		return false;
	}
	BLContext ctx(img);
	ctx.clearAll();
	auto toBl = [&](const glm::vec4& c) {
		return BLRgba32(static_cast<std::uint32_t>(std::lround(std::clamp(c.r, 0.f, 1.f) * 255.f)),
						static_cast<std::uint32_t>(std::lround(std::clamp(c.g, 0.f, 1.f) * 255.f)),
						static_cast<std::uint32_t>(std::lround(std::clamp(c.b, 0.f, 1.f) * 255.f)),
						static_cast<std::uint32_t>(std::lround(std::clamp(c.a, 0.f, 1.f) * 255.f)));
	};
	for (const auto& layer : doc.layers) {
		const auto& bag = layer.paths;
		for (size_t i = 0; i < bag.paths.size(); ++i) {
			const auto contours = plot::flattenPathContours(bag.paths[i], 0.25f);
			if (contours.empty()) {
				continue;
			}
			BLPath path;
			for (const auto& c : contours) {
				if (c.empty()) {
					continue;
				}
				path.moveTo(c[0].x * tile.scale + tile.ox, c[0].y * tile.scale + tile.oy);
				for (size_t k = 1; k < c.size(); ++k) {
					path.lineTo(c[k].x * tile.scale + tile.ox, c[k].y * tile.scale + tile.oy);
				}
			}
			if (bag.isFilled(i)) {
				const glm::vec4 solid =
					i < bag.pathColors.size() ? bag.pathColors[i] : layer.layerColor;
				ctx.setFillStyle(toBl(solid));
				ctx.setFillRule(bag.isEvenOdd(i) ? BL_FILL_RULE_EVEN_ODD : BL_FILL_RULE_NON_ZERO);
				ctx.fillPath(path);
			}
			if (bag.isStroked(i)) {
				const glm::vec4 solid =
					i < bag.pathStrokeColors.size() ? bag.pathStrokeColors[i] : layer.layerColor;
				ctx.setStrokeStyle(toBl(solid));
				ctx.setStrokeWidth(std::max(1.f, bag.strokeWidth(i, 1.f) * tile.scale));
				ctx.strokePath(path);
			}
		}
	}
	ctx.end();
	BLImageData data;
	if (img.getData(&data) != BL_SUCCESS || data.pixelData == nullptr) {
		return false;
	}
	const auto* src = static_cast<const std::uint8_t*>(data.pixelData);
	for (int y = 0; y < tile.h; ++y) {
		const auto* row = src + static_cast<size_t>(y) * static_cast<size_t>(data.stride);
		for (int x = 0; x < tile.w; ++x) {
			const std::uint32_t px = reinterpret_cast<const std::uint32_t*>(row)[x];
			const std::uint8_t b = static_cast<std::uint8_t>(px);
			const std::uint8_t g = static_cast<std::uint8_t>(px >> 8);
			const std::uint8_t r = static_cast<std::uint8_t>(px >> 16);
			const std::uint8_t a = static_cast<std::uint8_t>(px >> 24);
			const size_t i =
				(static_cast<size_t>(y) * static_cast<size_t>(tile.w) + static_cast<size_t>(x)) * 4;
			tile.rgba[i + 0] = r;
			tile.rgba[i + 1] = g;
			tile.rgba[i + 2] = b;
			tile.rgba[i + 3] = a;
		}
	}
	return true;
}
#endif

void rasterCoverage(const plot::ParsedPathDoc& doc, Tile& tile) {
	for (const auto& layer : doc.layers) {
		const auto& bag = layer.paths;
		for (size_t i = 0; i < bag.paths.size(); ++i) {
			if (bag.paths[i].empty()) {
				continue;
			}
			fillPath(tile, doc, bag, i);
		}
	}
	drawTexts(tile, doc);
}

bool measureDoc(const plot::ParsedPathDoc& doc, float& x0, float& y0, float& w, float& h) {
	bool any = false;
	float minX = 0.f;
	float minY = 0.f;
	float maxX = 0.f;
	float maxY = 0.f;
	auto grow = [&](float x, float y) {
		if (!any) {
			minX = maxX = x;
			minY = maxY = y;
			any = true;
			return;
		}
		minX = std::min(minX, x);
		minY = std::min(minY, y);
		maxX = std::max(maxX, x);
		maxY = std::max(maxY, y);
	};
	for (const auto& layer : doc.layers) {
		for (const auto& p : layer.paths.paths) {
			const plot::PathBounds b = plot::boundsOfPath(p, 0.5f);
			if (b.empty) {
				continue;
			}
			grow(b.minX, b.minY);
			grow(b.maxX, b.maxY);
		}
		for (const auto& t : layer.texts.items) {
			grow(t.baseline.x, t.baseline.y);
			grow(t.baseline.x + t.size * static_cast<float>(t.text.size()) * 0.6f,
				 t.baseline.y - t.size);
		}
	}
	if (doc.contentW > 0.f && doc.contentH > 0.f) {
		x0 = 0.f;
		y0 = 0.f;
		w = doc.contentW;
		h = doc.contentH;
		return true;
	}
	if (!any) {
		return false;
	}
	x0 = minX;
	y0 = minY;
	w = std::max(1.f, maxX - minX);
	h = std::max(1.f, maxY - minY);
	return true;
}

} // namespace

bool rasterizeFileThumbnail(const std::string& pathUtf8, int maxEdge, int& w, int& h,
							std::vector<std::uint8_t>& rgba) {
	w = 0;
	h = 0;
	rgba.clear();
	if (pathUtf8.empty() || maxEdge < 1) {
		return false;
	}

	plot::ParsedPathDoc doc;
	if (!loadFile(pathUtf8, doc)) {
		return false;
	}

	float x0 = 0.f;
	float y0 = 0.f;
	float srcW = 0.f;
	float srcH = 0.f;
	if (!measureDoc(doc, x0, y0, srcW, srcH)) {
		return false;
	}
	const float edge = std::max(srcW, srcH);
	const float scale =
		(edge > static_cast<float>(maxEdge)) ? (static_cast<float>(maxEdge) / edge) : 1.f;
	Tile tile;
	tile.w = std::max(1, static_cast<int>(srcW * scale + 0.5f));
	tile.h = std::max(1, static_cast<int>(srcH * scale + 0.5f));
	tile.scale = scale;
	tile.ox = -x0 * scale;
	tile.oy = -y0 * scale;
	tile.rgba.assign(static_cast<size_t>(tile.w) * static_cast<size_t>(tile.h) * 4, 0);

#ifdef RIGSVG_HAS_BLEND2D
	if (!rasterBlend2D(doc, tile)) {
		rasterCoverage(doc, tile);
	} else {
		drawTexts(tile, doc);
	}
#else
	rasterCoverage(doc, tile);
#endif

	w = tile.w;
	h = tile.h;
	rgba = std::move(tile.rgba);
	return true;
}

} // namespace svg
} // namespace rigkit
