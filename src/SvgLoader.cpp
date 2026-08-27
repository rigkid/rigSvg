#include "SvgLoader.h"

#include "PathBounds.h"
#include "PathEllipticArc.h"
#include "SvgPathParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

namespace rigkit {
namespace svg {
namespace {

struct Mat2d {
	float a = 1.f, b = 0.f, c = 0.f, d = 1.f, e = 0.f, f = 0.f;

	glm::vec2 apply(const glm::vec2& p) const {
		return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
	}

	/** @brief Average axis length — SVG stroke-width follows the CTM. */
	float strokeScale() const {
		const float sx = std::sqrt(a * a + b * b);
		const float sy = std::sqrt(c * c + d * d);
		return 0.5f * (sx + sy);
	}

	Mat2d then(const Mat2d& o) const {
		Mat2d r;
		r.a = a * o.a + c * o.b;
		r.b = b * o.a + d * o.b;
		r.c = a * o.c + c * o.d;
		r.d = b * o.c + d * o.d;
		r.e = a * o.e + c * o.f + e;
		r.f = b * o.e + d * o.f + f;
		return r;
	}
};

void transformPath(ecs::CPath& path, const Mat2d& m) {
	for (auto& cmd : path.commands) {
		if (cmd.type == ecs::CPath::Cmd::Close) {
			continue;
		}
		cmd.p = m.apply(cmd.p);
		if (cmd.type == ecs::CPath::Cmd::CubicTo || cmd.type == ecs::CPath::Cmd::QuadTo) {
			cmd.c1 = m.apply(cmd.c1);
		}
		if (cmd.type == ecs::CPath::Cmd::CubicTo) {
			cmd.c2 = m.apply(cmd.c2);
		}
		if (cmd.type == ecs::CPath::Cmd::ArcTo) {
			path::applyLinear(cmd, m.a, m.b, m.c, m.d);
		}
	}
}

float parseFloat(const std::string& s, float fallback = 0.f) {
	if (s.empty()) {
		return fallback;
	}
	try {
		return std::stof(s);
	} catch (...) {
		return fallback;
	}
}

bool parseMat2d(const std::string& transform, Mat2d& out) {
	out = {};
	if (transform.empty()) {
		return true;
	}
	std::regex re(R"((matrix|translate|scale|rotate)\s*\(\s*([^)]*)\))", std::regex::icase);
	auto begin = std::sregex_iterator(transform.begin(), transform.end(), re);
	auto end = std::sregex_iterator();
	Mat2d acc;
	for (auto it = begin; it != end; ++it) {
		std::string kind = (*it)[1].str();
		for (char& c : kind) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		std::string args = (*it)[2].str();
		for (char& c : args) {
			if (c == ',') {
				c = ' ';
			}
		}
		std::istringstream iss(args);
		std::vector<float> v;
		float n = 0.f;
		while (iss >> n) {
			v.push_back(n);
		}
		Mat2d step;
		if (kind == "matrix" && v.size() >= 6) {
			step = {v[0], v[1], v[2], v[3], v[4], v[5]};
		} else if (kind == "translate") {
			const float tx = v.empty() ? 0.f : v[0];
			const float ty = v.size() > 1 ? v[1] : 0.f;
			step = {1, 0, 0, 1, tx, ty};
		} else if (kind == "scale") {
			const float sx = v.empty() ? 1.f : v[0];
			const float sy = v.size() > 1 ? v[1] : sx;
			step = {sx, 0, 0, sy, 0, 0};
		} else if (kind == "rotate" && !v.empty()) {
			const float rad = v[0] * 0.01745329252f;
			const float c = std::cos(rad);
			const float s = std::sin(rad);
			const float cx = v.size() > 2 ? v[1] : 0.f;
			const float cy = v.size() > 2 ? v[2] : 0.f;
			Mat2d toOrigin{1, 0, 0, 1, -cx, -cy};
			Mat2d rot{c, s, -s, c, 0, 0};
			Mat2d fromOrigin{1, 0, 0, 1, cx, cy};
			step = fromOrigin.then(rot.then(toOrigin));
		} else {
			continue;
		}
		acc = acc.then(step);
	}
	out = acc;
	return true;
}

void recomputeBounds(plot::ParsedPathDoc& doc) {
	bool any = false;
	float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
	for (const auto& layer : doc.layers) {
		for (const auto& path : layer.paths.paths) {
			for (const auto& cmd : path.commands) {
				if (cmd.type == ecs::CPath::Cmd::Close) {
					continue;
				}
				if (!any) {
					minX = maxX = cmd.p.x;
					minY = maxY = cmd.p.y;
					any = true;
				} else {
					minX = std::min(minX, cmd.p.x);
					minY = std::min(minY, cmd.p.y);
					maxX = std::max(maxX, cmd.p.x);
					maxY = std::max(maxY, cmd.p.y);
				}
			}
		}
	}
	doc.minX = minX;
	doc.minY = minY;
	const float spanW = any ? (maxX - minX) : 0.f;
	const float spanH = any ? (maxY - minY) : 0.f;
	// Keep a prior canvas hint (viewBox / width / height) when larger than the bbox span.
	doc.contentW = std::max(doc.contentW, std::max(spanW, any ? maxX : 0.f));
	doc.contentH = std::max(doc.contentH, std::max(spanH, any ? maxY : 0.f));
}

/** @brief Attr lookup that won't match trailing letters of other keys (e.g. id vs d). */
std::string attrValue(const std::string& tag, const char* key) {
	const std::string pattern =
		std::string(R"((?:^|[^A-Za-z0-9_:\-]))") + key + R"(\s*=\s*["']([^"']*)["'])";
	std::regex re(pattern, std::regex::icase);
	std::smatch m;
	if (std::regex_search(tag, m, re)) {
		return m[1].str();
	}
	return {};
}

std::string layerNameFromGroup(const std::string& tag) {
	std::string name = attrValue(tag, "inkscape:label");
	if (name.empty()) {
		name = attrValue(tag, "id");
	}
	if (name.empty()) {
		name = "Group";
	}
	return name;
}

plot::PathSourceLayer& layerByName(plot::ParsedPathDoc& doc, const std::string& name) {
	for (auto& layer : doc.layers) {
		if (layer.name == name) {
			return layer;
		}
	}
	plot::PathSourceLayer layer;
	layer.name = name;
	doc.layers.push_back(std::move(layer));
	return doc.layers.back();
}

bool startsWithTag(const std::string& s, size_t i, const char* tag) {
	const size_t n = std::strlen(tag);
	if (i + n > s.size()) {
		return false;
	}
	for (size_t k = 0; k < n; ++k) {
		if (std::tolower(static_cast<unsigned char>(s[i + k])) !=
			std::tolower(static_cast<unsigned char>(tag[k]))) {
			return false;
		}
	}
	return true;
}

bool isTag(const std::string& tag, const char* name) {
	if (!startsWithTag(tag, 1, name)) {
		return false;
	}
	const size_t n = std::strlen(name);
	if (tag.size() <= 1 + n) {
		return true;
	}
	const char next = tag[1 + n];
	return !std::isalnum(static_cast<unsigned char>(next)) && next != '-' && next != ':';
}

std::string toLower(std::string s) {
	for (char& c : s) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

bool parseHexColor(const std::string& s, glm::vec4& out) {
	if (s.empty() || s[0] != '#') {
		return false;
	}
	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (c >= 'a' && c <= 'f') {
			return 10 + c - 'a';
		}
		return -1;
	};
	const std::string h = s.substr(1);
	auto ch = [&](size_t i) { return hex(h[i]); };
	if (h.size() == 3 || h.size() == 4) {
		const int r = ch(0), g = ch(1), b = ch(2);
		if (r < 0 || g < 0 || b < 0) {
			return false;
		}
		const int a = h.size() == 4 ? ch(3) : 15;
		if (a < 0) {
			return false;
		}
		out = {r / 15.f, g / 15.f, b / 15.f, a / 15.f};
		return true;
	}
	if (h.size() == 6 || h.size() == 8) {
		const int r = ch(0) * 16 + ch(1);
		const int g = ch(2) * 16 + ch(3);
		const int b = ch(4) * 16 + ch(5);
		if (r < 0 || g < 0 || b < 0) {
			return false;
		}
		int a = 255;
		if (h.size() == 8) {
			a = ch(6) * 16 + ch(7);
			if (a < 0) {
				return false;
			}
		}
		out = {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
		return true;
	}
	return false;
}

bool parseColor(const std::string& raw, glm::vec4& out) {
	std::string s = raw;
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
		s.erase(s.begin());
	}
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
		s.pop_back();
	}
	const std::string lower = toLower(s);
	if (lower.empty() || lower == "none" || lower == "transparent") {
		return false;
	}
	if (parseHexColor(lower, out)) {
		return true;
	}
	if (lower == "black") {
		out = {0, 0, 0, 1};
		return true;
	}
	if (lower == "white") {
		out = {1, 1, 1, 1};
		return true;
	}
	if (lower.rfind("rgb", 0) == 0) {
		std::regex re(
			R"(rgba?\(\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)(?:\s*,\s*([0-9.]+))?\s*\))",
			std::regex::icase);
		std::smatch m;
		if (std::regex_match(s, m, re)) {
			out.r = parseFloat(m[1].str()) / 255.f;
			out.g = parseFloat(m[2].str()) / 255.f;
			out.b = parseFloat(m[3].str()) / 255.f;
			out.a = m[4].matched ? parseFloat(m[4].str()) : 1.f;
			if (out.a > 1.f) {
				out.a /= 255.f;
			}
			return true;
		}
	}
	return false;
}

std::string extractUrlId(const std::string& v) {
	std::regex re(R"(url\(\s*#([^)\s]+)\s*\))", std::regex::icase);
	std::smatch m;
	if (std::regex_search(v, m, re)) {
		return m[1].str();
	}
	return {};
}

std::string styleProp(const std::string& style, const char* key) {
	const std::string pattern = std::string(R"((?:^|;)\s*)") + key + R"(\s*:\s*([^;]+))";
	std::regex re(pattern, std::regex::icase);
	std::smatch m;
	if (std::regex_search(style, m, re)) {
		std::string v = m[1].str();
		while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) {
			v.erase(v.begin());
		}
		while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) {
			v.pop_back();
		}
		return v;
	}
	return {};
}

struct PaintSpec {
	bool filled = true;
	bool stroked = false;
	glm::vec4 fillColor{0.f, 0.f, 0.f, 1.f};
	glm::vec4 strokeColor{0.f, 0.f, 0.f, 1.f};
	float strokeWidth = 1.f;
	std::string fillUrl;
	std::string strokeUrl;
	bool evenOdd = false;
	bool hasFill = false;
	bool hasStroke = false;
	bool hasStrokeWidth = false;
	bool hasFillRule = false;
};

/** @brief Parse only attributes present on @p tag (for inheritance merge). */
PaintSpec paintOverridesFromTag(const std::string& tag) {
	PaintSpec p;
	const std::string style = attrValue(tag, "style");
	auto get = [&](const char* key) {
		std::string v = attrValue(tag, key);
		if (v.empty() && !style.empty()) {
			v = styleProp(style, key);
		}
		return v;
	};

	const std::string fill = get("fill");
	if (!fill.empty()) {
		p.hasFill = true;
		const std::string url = extractUrlId(fill);
		if (!url.empty()) {
			p.filled = true;
			p.fillUrl = url;
		} else if (toLower(fill) == "none") {
			p.filled = false;
			p.fillUrl.clear();
		} else if (parseColor(fill, p.fillColor)) {
			p.filled = true;
			p.fillUrl.clear();
		} else {
			p.hasFill = false;
		}
	}

	const std::string stroke = get("stroke");
	if (!stroke.empty()) {
		p.hasStroke = true;
		const std::string url = extractUrlId(stroke);
		if (!url.empty()) {
			p.stroked = true;
			p.strokeUrl = url;
		} else if (toLower(stroke) == "none") {
			p.stroked = false;
			p.strokeUrl.clear();
		} else if (parseColor(stroke, p.strokeColor)) {
			p.stroked = true;
			p.strokeUrl.clear();
		} else {
			p.hasStroke = false;
		}
	}

	const std::string sw = get("stroke-width");
	if (!sw.empty()) {
		p.hasStrokeWidth = true;
		p.strokeWidth = parseFloat(sw, p.strokeWidth);
	}

	const std::string fr = get("fill-rule");
	if (!fr.empty()) {
		const std::string l = toLower(fr);
		if (l == "evenodd") {
			p.evenOdd = true;
			p.hasFillRule = true;
		} else if (l == "nonzero") {
			p.evenOdd = false;
			p.hasFillRule = true;
		}
	}
	return p;
}

/** @brief SVG paint inheritance: child overrides replace parent fields. */
PaintSpec resolvePaint(const PaintSpec& parent, const PaintSpec& local) {
	PaintSpec out = parent;
	if (local.hasFill) {
		out.filled = local.filled;
		out.fillColor = local.fillColor;
		out.fillUrl = local.fillUrl;
		out.hasFill = true;
	}
	if (local.hasStroke) {
		out.stroked = local.stroked;
		out.strokeColor = local.strokeColor;
		out.strokeUrl = local.strokeUrl;
		out.hasStroke = true;
	}
	if (local.hasStrokeWidth) {
		out.strokeWidth = local.strokeWidth;
		out.hasStrokeWidth = true;
	}
	if (local.hasFillRule) {
		out.evenOdd = local.evenOdd;
		out.hasFillRule = true;
	}
	return out;
}

struct SvgGradDef {
	bool radial = false;
	std::string id;
	float x1 = 0.f, y1 = 0.f, x2 = 1.f, y2 = 0.f;
	float cx = 0.f, cy = 0.f, r = 1.f;
	ecs::CGradient::Spread spread = ecs::CGradient::Spread::Pad;
	std::vector<ecs::CGradientStop> stops;
};

ecs::CGradient::Spread parseSpread(const std::string& s) {
	const std::string l = toLower(s);
	if (l == "repeat") {
		return ecs::CGradient::Spread::Repeat;
	}
	if (l == "reflect" || l == "mirror") {
		return ecs::CGradient::Spread::Mirror;
	}
	return ecs::CGradient::Spread::Pad;
}

float parseOffset(const std::string& s) {
	if (s.empty()) {
		return 0.f;
	}
	if (s.back() == '%') {
		return parseFloat(s.substr(0, s.size() - 1)) / 100.f;
	}
	return parseFloat(s);
}

int ensureGradient(plot::ParsedPathDoc& doc, const SvgGradDef& def, const plot::PathBounds& b,
				   std::unordered_map<std::string, int>& idMap) {
	auto it = idMap.find(def.id);
	if (it != idMap.end()) {
		return it->second;
	}
	ecs::CGradient g;
	g.name = def.id.empty() ? "Gradient" : def.id;
	g.stops = def.stops;
	g.spread = def.spread;
	g.interp = ecs::CGradient::Interp::RGB;
	if (def.radial) {
		g.kind = ecs::CGradient::Kind::Radial;
		const float w = std::max(1e-4f, b.maxX - b.minX);
		const float h = std::max(1e-4f, b.maxY - b.minY);
		g.center = {(def.cx - b.minX) / w, (def.cy - b.minY) / h};
		g.outerRadius = def.r / std::max(w, h);
		g.innerRadius = 0.f;
	} else {
		g.kind = ecs::CGradient::Kind::Linear;
		g.angle = std::atan2(def.y2 - def.y1, def.x2 - def.x1) * 57.2957795f;
	}
	const int idx = static_cast<int>(doc.paints.gradients.size());
	doc.paints.gradients.push_back(std::move(g));
	if (!def.id.empty()) {
		idMap[def.id] = idx;
	}
	return idx;
}

void appendPaintedPath(plot::ParsedPathDoc& doc, const std::string& layer, ecs::CPath path,
					   const Mat2d& xform, const PaintSpec& paint,
					   const std::unordered_map<std::string, SvgGradDef>& grads,
					   std::unordered_map<std::string, int>& gradIds) {
	transformPath(path, xform);
	if (path.commands.empty()) {
		return;
	}
	auto& bag = layerByName(doc, layer).paths;
	const size_t i = bag.paths.size();
	bag.paths.push_back(std::move(path));

	const plot::PathBounds b = plot::boundsOfPath(bag.paths.back());

	bag.pathFilled.resize(i + 1, 0);
	bag.pathStroked.resize(i + 1, 0);
	bag.pathColors.resize(i + 1, glm::vec4{0, 0, 0, 1});
	bag.pathStrokeColors.resize(i + 1, glm::vec4{0, 0, 0, 1});
	bag.pathStrokeWidths.resize(i + 1, 1.f);
	bag.pathFillPaint.resize(i + 1, ecs::kPaintSolid);
	bag.pathStrokePaint.resize(i + 1, ecs::kPaintSolid);
	bag.pathFillRule.resize(i + 1, 0);

	bag.pathFilled[i] = paint.filled ? 1 : 0;
	bag.pathStroked[i] = paint.stroked ? 1 : 0;
	bag.pathFillRule[i] = paint.evenOdd ? 1 : 0;
	bag.pathColors[i] = paint.fillColor;
	bag.pathStrokeColors[i] = paint.strokeColor;
	bag.pathStrokeWidths[i] = paint.strokeWidth * std::max(1e-6f, xform.strokeScale());

	if (paint.filled && !paint.fillUrl.empty()) {
		auto git = grads.find(paint.fillUrl);
		if (git != grads.end()) {
			bag.pathFillPaint[i] = ensureGradient(doc, git->second, b, gradIds);
		}
	}
	if (paint.stroked && !paint.strokeUrl.empty()) {
		auto git = grads.find(paint.strokeUrl);
		if (git != grads.end()) {
			bag.pathStrokePaint[i] = ensureGradient(doc, git->second, b, gradIds);
		}
	}
}

ecs::CPath makeLine(float x1, float y1, float x2, float y2) {
	ecs::CPath path;
	using Cmd = ecs::CPath::Cmd;
	path.commands.push_back({Cmd::MoveTo, {x1, y1}, {}, {}});
	path.commands.push_back({Cmd::LineTo, {x2, y2}, {}, {}});
	return path;
}

ecs::CPath makeRect(float x, float y, float w, float h) {
	ecs::CPath path;
	using Cmd = ecs::CPath::Cmd;
	path.commands.push_back({Cmd::MoveTo, {x, y}, {}, {}});
	path.commands.push_back({Cmd::LineTo, {x + w, y}, {}, {}});
	path.commands.push_back({Cmd::LineTo, {x + w, y + h}, {}, {}});
	path.commands.push_back({Cmd::LineTo, {x, y + h}, {}, {}});
	path.commands.push_back({Cmd::Close, {}, {}, {}});
	return path;
}

ecs::CPath makeEllipse(float cx, float cy, float rx, float ry) {
	ecs::CPath path;
	if (rx <= 0.f || ry <= 0.f) {
		return path;
	}
	using Cmd = ecs::CPath::Cmd;
	const glm::vec2 right{cx + rx, cy};
	const glm::vec2 left{cx - rx, cy};
	path.commands.push_back({Cmd::MoveTo, right, {}, {}});
	path.commands.push_back(path::makeArcTo(left, rx, ry, 0.f, true, true));
	path.commands.push_back(path::makeArcTo(right, rx, ry, 0.f, true, true));
	path.commands.push_back({Cmd::Close, {}, {}, {}});
	return path;
}

ecs::CPath makePoly(const std::string& points, bool closed) {
	ecs::CPath path;
	std::string s = points;
	for (char& c : s) {
		if (c == ',') {
			c = ' ';
		}
	}
	std::istringstream iss(s);
	std::vector<glm::vec2> pts;
	float x = 0.f, y = 0.f;
	while (iss >> x >> y) {
		pts.push_back({x, y});
	}
	if (pts.size() < 2) {
		return path;
	}
	using Cmd = ecs::CPath::Cmd;
	path.commands.push_back({Cmd::MoveTo, pts.front(), {}, {}});
	for (size_t i = 1; i < pts.size(); ++i) {
		path.commands.push_back({Cmd::LineTo, pts[i], {}, {}});
	}
	if (closed) {
		path.commands.push_back({Cmd::Close, {}, {}, {}});
	}
	return path;
}

bool tryShape(const std::string& tag, plot::ParsedPathDoc& doc, const std::string& layer,
			  const Mat2d& parentXform, const PaintSpec& parentPaint,
			  const std::unordered_map<std::string, SvgGradDef>& grads,
			  std::unordered_map<std::string, int>& gradIds) {
	Mat2d local;
	parseMat2d(attrValue(tag, "transform"), local);
	const Mat2d xform = parentXform.then(local);
	const PaintSpec paint = resolvePaint(parentPaint, paintOverridesFromTag(tag));

	auto push = [&](ecs::CPath path) {
		appendPaintedPath(doc, layer, std::move(path), xform, paint, grads, gradIds);
	};

	if (isTag(tag, "path")) {
		const std::string d = attrValue(tag, "d");
		if (d.empty()) {
			return true;
		}
		ecs::CPath path;
		if (parsePathD(d, path)) {
			push(std::move(path));
		}
		return true;
	}
	if (isTag(tag, "line")) {
		push(makeLine(parseFloat(attrValue(tag, "x1")), parseFloat(attrValue(tag, "y1")),
					  parseFloat(attrValue(tag, "x2")), parseFloat(attrValue(tag, "y2"))));
		return true;
	}
	if (isTag(tag, "rect")) {
		push(makeRect(parseFloat(attrValue(tag, "x")), parseFloat(attrValue(tag, "y")),
					  parseFloat(attrValue(tag, "width")), parseFloat(attrValue(tag, "height"))));
		return true;
	}
	if (isTag(tag, "circle")) {
		const float r = parseFloat(attrValue(tag, "r"));
		push(makeEllipse(parseFloat(attrValue(tag, "cx")), parseFloat(attrValue(tag, "cy")), r, r));
		return true;
	}
	if (isTag(tag, "ellipse")) {
		push(makeEllipse(parseFloat(attrValue(tag, "cx")), parseFloat(attrValue(tag, "cy")),
						 parseFloat(attrValue(tag, "rx")), parseFloat(attrValue(tag, "ry"))));
		return true;
	}
	if (isTag(tag, "polyline")) {
		push(makePoly(attrValue(tag, "points"), false));
		return true;
	}
	if (isTag(tag, "polygon")) {
		push(makePoly(attrValue(tag, "points"), true));
		return true;
	}
	return false;
}

void beginGradient(const std::string& tag, bool radial, SvgGradDef& cur, bool& active) {
	cur = {};
	cur.radial = radial;
	cur.id = attrValue(tag, "id");
	cur.spread = parseSpread(attrValue(tag, "spreadMethod"));
	if (radial) {
		cur.cx = parseFloat(attrValue(tag, "cx"));
		cur.cy = parseFloat(attrValue(tag, "cy"));
		cur.r = parseFloat(attrValue(tag, "r"), 1.f);
	} else {
		cur.x1 = parseFloat(attrValue(tag, "x1"));
		cur.y1 = parseFloat(attrValue(tag, "y1"));
		cur.x2 = parseFloat(attrValue(tag, "x2"), 1.f);
		cur.y2 = parseFloat(attrValue(tag, "y2"));
	}
	active = true;
}

} // namespace

bool loadString(const std::string& svgText, plot::ParsedPathDoc& out) {
	out = {};
	std::stack<std::string> groups;
	std::stack<Mat2d> xforms;
	std::stack<PaintSpec> paints;
	groups.push("SVG");
	xforms.push({});
	paints.push({}); // SVG initial: fill black, stroke none

	std::unordered_map<std::string, SvgGradDef> grads;
	std::unordered_map<std::string, int> gradIds;
	SvgGradDef curGrad;
	bool inGrad = false;

	size_t i = 0;
	while (i < svgText.size()) {
		if (svgText[i] != '<') {
			++i;
			continue;
		}
		if (i + 3 < svgText.size() && svgText[i + 1] == '!' && svgText[i + 2] == '-' &&
			svgText[i + 3] == '-') {
			const size_t end = svgText.find("-->", i + 4);
			i = end == std::string::npos ? svgText.size() : end + 3;
			continue;
		}
		const size_t end = svgText.find('>', i);
		if (end == std::string::npos) {
			break;
		}
		const std::string tag = svgText.substr(i, end - i + 1);
		i = end + 1;

		if (startsWithTag(tag, 1, "/linearGradient") || startsWithTag(tag, 1, "/radialGradient")) {
			if (inGrad && !curGrad.id.empty()) {
				grads[curGrad.id] = curGrad;
			}
			inGrad = false;
			continue;
		}
		if (isTag(tag, "linearGradient")) {
			beginGradient(tag, false, curGrad, inGrad);
			if (!tag.empty() && tag[tag.size() - 2] == '/') {
				if (!curGrad.id.empty()) {
					grads[curGrad.id] = curGrad;
				}
				inGrad = false;
			}
			continue;
		}
		if (isTag(tag, "radialGradient")) {
			beginGradient(tag, true, curGrad, inGrad);
			if (!tag.empty() && tag[tag.size() - 2] == '/') {
				if (!curGrad.id.empty()) {
					grads[curGrad.id] = curGrad;
				}
				inGrad = false;
			}
			continue;
		}
		if (inGrad && isTag(tag, "stop")) {
			ecs::CGradientStop stop;
			stop.t = parseOffset(attrValue(tag, "offset"));
			std::string sc = attrValue(tag, "stop-color");
			if (sc.empty()) {
				sc = styleProp(attrValue(tag, "style"), "stop-color");
			}
			if (!parseColor(sc, stop.rgba)) {
				stop.rgba = {0, 0, 0, 1};
			}
			std::string sa = attrValue(tag, "stop-opacity");
			if (sa.empty()) {
				sa = styleProp(attrValue(tag, "style"), "stop-opacity");
			}
			if (!sa.empty()) {
				stop.rgba.a *= parseFloat(sa, 1.f);
			}
			curGrad.stops.push_back(stop);
			continue;
		}

		if (startsWithTag(tag, 1, "/g")) {
			if (groups.size() > 1) {
				groups.pop();
				xforms.pop();
				paints.pop();
			}
			continue;
		}
		if (isTag(tag, "g")) {
			// Nested groups inherit paint/transform but stay on the same layer —
			// Ghostscript Tiger has hundreds of fill-only <g> wrappers per path.
			const std::string name = (groups.size() <= 1) ? layerNameFromGroup(tag) : groups.top();
			groups.push(name);
			Mat2d local;
			parseMat2d(attrValue(tag, "transform"), local);
			xforms.push(xforms.top().then(local));
			paints.push(resolvePaint(paints.top(), paintOverridesFromTag(tag)));
			continue;
		}
		if (isTag(tag, "svg")) {
			Mat2d local;
			parseMat2d(attrValue(tag, "transform"), local);
			const std::string vb = attrValue(tag, "viewBox");
			float canvasW = 0.f;
			float canvasH = 0.f;
			if (!vb.empty()) {
				std::string s = vb;
				for (char& c : s) {
					if (c == ',') {
						c = ' ';
					}
				}
				std::istringstream iss(s);
				float minX = 0.f, minY = 0.f, vbW = 0.f, vbH = 0.f;
				if (iss >> minX >> minY >> vbW >> vbH && vbW > 0.f && vbH > 0.f) {
					float outW = vbW;
					float outH = vbH;
					auto stripUnit = [](std::string v) {
						while (!v.empty() && !std::isdigit(static_cast<unsigned char>(v.back())) &&
							   v.back() != '.' && v.back() != '-') {
							v.pop_back();
						}
						return v;
					};
					const std::string wAttr = attrValue(tag, "width");
					const std::string hAttr = attrValue(tag, "height");
					if (!wAttr.empty()) {
						outW = parseFloat(stripUnit(wAttr), vbW);
					}
					if (!hAttr.empty()) {
						outH = parseFloat(stripUnit(hAttr), vbH);
					}
					canvasW = outW;
					canvasH = outH;
					const float sx = outW / vbW;
					const float sy = outH / vbH;
					Mat2d view;
					view.a = sx;
					view.d = sy;
					view.e = -minX * sx;
					view.f = -minY * sy;
					local = local.then(view);
				}
			} else {
				auto stripUnit = [](std::string v) {
					while (!v.empty() && !std::isdigit(static_cast<unsigned char>(v.back())) &&
						   v.back() != '.' && v.back() != '-') {
						v.pop_back();
					}
					return v;
				};
				const std::string wAttr = attrValue(tag, "width");
				const std::string hAttr = attrValue(tag, "height");
				if (!wAttr.empty()) {
					canvasW = parseFloat(stripUnit(wAttr), 0.f);
				}
				if (!hAttr.empty()) {
					canvasH = parseFloat(stripUnit(hAttr), 0.f);
				}
			}
			if (canvasW > 0.f) {
				out.contentW = canvasW;
			}
			if (canvasH > 0.f) {
				out.contentH = canvasH;
			}
			xforms.top() = xforms.top().then(local);
			continue;
		}

		tryShape(tag, out, groups.top(), xforms.top(), paints.top(), grads, gradIds);
	}

	out.layers.erase(std::remove_if(out.layers.begin(), out.layers.end(),
									[](const plot::PathSourceLayer& l) { return l.paths.empty(); }),
					 out.layers.end());

	if (out.layers.empty()) {
		return false;
	}
	recomputeBounds(out);
	return true;
}

bool loadFile(const std::string& path, plot::ParsedPathDoc& out) {
	namespace fs = std::filesystem;
	std::error_code ec;
#if defined(__cpp_lib_char8_t)
	const fs::path filePath = fs::path(reinterpret_cast<const char8_t*>(path.c_str()));
#else
	const fs::path filePath = fs::u8path(path);
#endif
	if (!fs::is_regular_file(filePath, ec)) {
		spdlog::error("[rigSvg] open failed '{}' ({})", path, ec.message());
		return false;
	}

	std::ifstream in(filePath, std::ios::binary);
	if (!in) {
		spdlog::error("[rigSvg] could not read '{}'", path);
		return false;
	}
	std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (raw.empty()) {
		spdlog::error("[rigSvg] empty file '{}'", path);
		return false;
	}

	// CorelDRAW and other hosts often emit UTF-16 SVG (BOM FF FE / FE FF).
	std::string utf8;
	const auto isBom16Le = raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF &&
						   static_cast<unsigned char>(raw[1]) == 0xFE;
	const auto isBom16Be = raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE &&
						   static_cast<unsigned char>(raw[1]) == 0xFF;
	if (isBom16Le || isBom16Be) {
		utf8.reserve(raw.size() / 2);
		size_t i = 2;
		while (i + 1 < raw.size()) {
			const unsigned char a = static_cast<unsigned char>(raw[i]);
			const unsigned char b = static_cast<unsigned char>(raw[i + 1]);
			const unsigned int cp = isBom16Le ? (a | (b << 8)) : ((a << 8) | b);
			i += 2;
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < raw.size()) {
				const unsigned char c = static_cast<unsigned char>(raw[i]);
				const unsigned char d = static_cast<unsigned char>(raw[i + 1]);
				const unsigned int low = isBom16Le ? (c | (d << 8)) : ((c << 8) | d);
				i += 2;
				if (low >= 0xDC00 && low <= 0xDFFF) {
					const unsigned int u = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
					utf8.push_back(static_cast<char>(0xF0 | (u >> 18)));
					utf8.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
					utf8.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
					utf8.push_back(static_cast<char>(0x80 | (u & 0x3F)));
					continue;
				}
			}
			if (cp < 0x80) {
				utf8.push_back(static_cast<char>(cp));
			} else if (cp < 0x800) {
				utf8.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			} else {
				utf8.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		if (!loadString(utf8, out)) {
			spdlog::error("[rigSvg] parse failed (UTF-16) '{}'", path);
			return false;
		}
		return true;
	}

	if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
		static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
		if (!loadString(raw.substr(3), out)) {
			spdlog::error("[rigSvg] parse failed (UTF-8 BOM) '{}'", path);
			return false;
		}
		return true;
	}
	if (!loadString(raw, out)) {
		spdlog::error("[rigSvg] parse failed '{}'", path);
		return false;
	}
	return true;
}

} // namespace svg
} // namespace rigkit
