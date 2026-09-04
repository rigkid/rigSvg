#include "SvgWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>
#include "PathBounds.h"

namespace rigkit {
namespace svg {
namespace {

std::string esc(const std::string& s) {
	std::string o;
	o.reserve(s.size());
	for (char c : s) {
		switch (c) {
		case '&':
			o += "&amp;";
			break;
		case '<':
			o += "&lt;";
			break;
		case '>':
			o += "&gt;";
			break;
		case '"':
			o += "&quot;";
			break;
		default:
			o += c;
			break;
		}
	}
	return o;
}

std::string colorCss(const glm::vec4& c) {
	const int r = static_cast<int>(std::lround(std::clamp(c.r, 0.f, 1.f) * 255.f));
	const int g = static_cast<int>(std::lround(std::clamp(c.g, 0.f, 1.f) * 255.f));
	const int b = static_cast<int>(std::lround(std::clamp(c.b, 0.f, 1.f) * 255.f));
	char buf[32];
	if (c.a < 0.999f) {
		std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", r, g, b,
					  static_cast<double>(std::clamp(c.a, 0.f, 1.f)));
	} else {
		std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
	}
	return buf;
}

float srgbToLin(float x) {
	return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}

float linToSrgb(float x) {
	x = std::max(0.f, x);
	return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f;
}

glm::vec3 toLinear(const glm::vec3& c) {
	return {srgbToLin(c.r), srgbToLin(c.g), srgbToLin(c.b)};
}

glm::vec3 toSrgb(const glm::vec3& c) {
	return {linToSrgb(c.r), linToSrgb(c.g), linToSrgb(c.b)};
}

glm::vec3 linearToOklab(const glm::vec3& c) {
	const float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
	const float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
	const float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;
	const float l_ = std::cbrt(l);
	const float m_ = std::cbrt(m);
	const float s_ = std::cbrt(s);
	return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
			1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
			0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

glm::vec3 oklabToLinear(const glm::vec3& lab) {
	const float l_ = lab.x + 0.3963377774f * lab.y + 0.2158037573f * lab.z;
	const float m_ = lab.x - 0.1055613458f * lab.y - 0.0638541728f * lab.z;
	const float s_ = lab.x - 0.0894841775f * lab.y - 1.2914855480f * lab.z;
	const float l = l_ * l_ * l_;
	const float m = m_ * m_ * m_;
	const float s = s_ * s_ * s_;
	return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
			-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
			-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

glm::vec3 rgbToHsv(const glm::vec3& c) {
	const float mx = std::max(c.r, std::max(c.g, c.b));
	const float mn = std::min(c.r, std::min(c.g, c.b));
	const float d = mx - mn;
	float h = 0.f;
	if (d > 1e-6f) {
		if (mx == c.r) {
			h = std::fmod((c.g - c.b) / d, 6.f);
		} else if (mx == c.g) {
			h = (c.b - c.r) / d + 2.f;
		} else {
			h = (c.r - c.g) / d + 4.f;
		}
		h /= 6.f;
		if (h < 0.f) {
			h += 1.f;
		}
	}
	return {h, mx > 1e-6f ? d / mx : 0.f, mx};
}

glm::vec3 hsvToRgb(const glm::vec3& hsv) {
	const float h = hsv.x * 6.f;
	const float s = hsv.y;
	const float v = hsv.z;
	const int i = static_cast<int>(std::floor(h)) % 6;
	const float f = h - std::floor(h);
	const float p = v * (1.f - s);
	const float q = v * (1.f - f * s);
	const float t = v * (1.f - (1.f - f) * s);
	switch (i) {
	case 0:
		return {v, t, p};
	case 1:
		return {q, v, p};
	case 2:
		return {p, v, t};
	case 3:
		return {p, q, v};
	case 4:
		return {t, p, v};
	default:
		return {v, p, q};
	}
}

struct SpaceColor {
	glm::vec3 v{0.f};
	float a = 1.f;
};

SpaceColor toSpace(const ecs::CGradientStop& s, ecs::CGradient::Interp interp) {
	const glm::vec3 lin = toLinear(glm::vec3(s.rgba)) * s.intensity;
	SpaceColor o;
	o.a = s.rgba.a;
	switch (interp) {
	case ecs::CGradient::Interp::OkLab:
		o.v = linearToOklab(lin);
		break;
	case ecs::CGradient::Interp::HSV:
		o.v = rgbToHsv(toSrgb(lin));
		break;
	default:
		o.v = lin;
		break;
	}
	return o;
}

SpaceColor mixSpace(const SpaceColor& a, const SpaceColor& b, float t,
					ecs::CGradient::Interp interp) {
	SpaceColor o;
	o.a = glm::mix(a.a, b.a, t);
	if (interp == ecs::CGradient::Interp::HSV) {
		float dh = b.v.x - a.v.x;
		if (dh > 0.5f) {
			dh -= 1.f;
		} else if (dh < -0.5f) {
			dh += 1.f;
		}
		o.v.x = a.v.x + dh * t;
		if (o.v.x < 0.f) {
			o.v.x += 1.f;
		} else if (o.v.x > 1.f) {
			o.v.x -= 1.f;
		}
		o.v.y = glm::mix(a.v.y, b.v.y, t);
		o.v.z = glm::mix(a.v.z, b.v.z, t);
	} else {
		o.v = glm::mix(a.v, b.v, t);
	}
	return o;
}

glm::vec4 fromSpace(const SpaceColor& c, ecs::CGradient::Interp interp) {
	glm::vec3 lin;
	switch (interp) {
	case ecs::CGradient::Interp::OkLab:
		lin = oklabToLinear(c.v);
		break;
	case ecs::CGradient::Interp::HSV:
		lin = toLinear(hsvToRgb(c.v));
		break;
	default:
		lin = c.v;
		break;
	}
	const glm::vec3 srgb = toSrgb(lin);
	return {std::clamp(srgb.r, 0.f, 1.f), std::clamp(srgb.g, 0.f, 1.f),
			std::clamp(srgb.b, 0.f, 1.f), std::clamp(c.a, 0.f, 1.f)};
}

float srgbDist(const glm::vec4& a, const glm::vec4& b) {
	return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

void densifySpan(std::vector<std::pair<float, SpaceColor>>& out, float t0, const SpaceColor& a,
				 float t1, const SpaceColor& b, ecs::CGradient::Interp interp, int depth) {
	if (depth <= 0 || (t1 - t0) < 0.02f) {
		return;
	}
	const float tm = 0.5f * (t0 + t1);
	const SpaceColor mid = mixSpace(a, b, 0.5f, interp);
	const glm::vec4 trueCol = fromSpace(mid, interp);
	const glm::vec4 fakeCol = glm::mix(fromSpace(a, interp), fromSpace(b, interp), 0.5f);
	if (srgbDist(trueCol, fakeCol) < 0.04f) {
		return;
	}
	densifySpan(out, t0, a, tm, mid, interp, depth - 1);
	out.push_back({tm, mid});
	densifySpan(out, tm, mid, t1, b, interp, depth - 1);
}

std::vector<std::pair<float, glm::vec4>> densifyStops(const ecs::CGradient& g) {
	std::vector<ecs::CGradientStop> src = g.stops;
	std::sort(src.begin(), src.end(),
			  [](const ecs::CGradientStop& a, const ecs::CGradientStop& b) { return a.t < b.t; });
	std::vector<std::pair<float, SpaceColor>> work;
	work.reserve(src.size() * 4);
	for (const auto& s : src) {
		work.push_back({std::clamp(s.t, 0.f, 1.f), toSpace(s, g.interp)});
	}
	if (g.interp != ecs::CGradient::Interp::RGB && work.size() >= 2) {
		std::vector<std::pair<float, SpaceColor>> extra;
		for (size_t i = 0; i + 1 < work.size(); ++i) {
			densifySpan(extra, work[i].first, work[i].second, work[i + 1].first, work[i + 1].second,
						g.interp, 6);
		}
		work.insert(work.end(), extra.begin(), extra.end());
		std::sort(work.begin(), work.end(),
				  [](const auto& a, const auto& b) { return a.first < b.first; });
	}
	std::vector<std::pair<float, glm::vec4>> out;
	out.reserve(work.size());
	for (const auto& w : work) {
		out.push_back({w.first, fromSpace(w.second, g.interp)});
	}
	return out;
}

std::string pathD(const ecs::CPath& path) {
	std::ostringstream ss;
	ss.setf(std::ios::fixed);
	ss.precision(4);
	for (const auto& cmd : path.commands) {
		using Cmd = ecs::CPath::Cmd;
		switch (cmd.type) {
		case Cmd::MoveTo:
			ss << "M" << cmd.p.x << " " << cmd.p.y << " ";
			break;
		case Cmd::LineTo:
			ss << "L" << cmd.p.x << " " << cmd.p.y << " ";
			break;
		case Cmd::CubicTo:
			ss << "C" << cmd.c1.x << " " << cmd.c1.y << " " << cmd.c2.x << " " << cmd.c2.y << " "
			   << cmd.p.x << " " << cmd.p.y << " ";
			break;
		case Cmd::QuadTo:
			ss << "Q" << cmd.c1.x << " " << cmd.c1.y << " " << cmd.p.x << " " << cmd.p.y << " ";
			break;
		case Cmd::Close:
			ss << "Z ";
			break;
		case Cmd::ArcTo:
			ss << "A" << cmd.radii.x << " " << cmd.radii.y << " " << cmd.xRotDeg << " "
			   << (cmd.largeArc ? 1 : 0) << " " << (cmd.sweep ? 1 : 0) << " " << cmd.p.x << " "
			   << cmd.p.y << " ";
			break;
		}
	}
	return ss.str();
}

const char* spreadStr(ecs::CGradient::Spread s) {
	switch (s) {
	case ecs::CGradient::Spread::Repeat:
		return "repeat";
	case ecs::CGradient::Spread::Mirror:
		return "reflect";
	default:
		return "pad";
	}
}

std::string emitGradient(std::ostringstream& defs, const ecs::CGradient& g,
						 const plot::PathBounds& b, int& nextId) {
	const std::string id = "g" + std::to_string(nextId++);
	float minX = b.minX, minY = b.minY, w = b.maxX - b.minX, h = b.maxY - b.minY;
	if (b.empty || w <= 0.f || h <= 0.f) {
		minX = 0.f;
		minY = 0.f;
		w = 1.f;
		h = 1.f;
	}

	auto writeStops = [&] {
		for (const auto& s : densifyStops(g)) {
			defs << "    <stop offset=\"" << (s.first * 100.f) << "%\" stop-color=\""
				 << colorCss(s.second) << "\"/>\n";
		}
	};

	if (g.kind == ecs::CGradient::Kind::Radial) {
		const float cx = minX + g.center.x * w;
		const float cy = minY + g.center.y * h;
		const float r = g.outerRadius * std::max(w, h);
		defs << "  <radialGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" cx=\"" << cx
			 << "\" cy=\"" << cy << "\" r=\"" << r << "\" fx=\"" << cx << "\" fy=\"" << cy
			 << "\" spreadMethod=\"" << spreadStr(g.spread) << "\">\n";
		writeStops();
		defs << "  </radialGradient>\n";
	} else {
		const float rad = g.angle * 0.01745329252f;
		const float cx = minX + 0.5f * w;
		const float cy = minY + 0.5f * h;
		const float half = 0.5f * std::sqrt(w * w + h * h);
		const float dx = std::cos(rad) * half;
		const float dy = std::sin(rad) * half;
		defs << "  <linearGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" x1=\""
			 << (cx - dx) << "\" y1=\"" << (cy - dy) << "\" x2=\"" << (cx + dx) << "\" y2=\""
			 << (cy + dy) << "\" spreadMethod=\"" << spreadStr(g.spread) << "\">\n";
		writeStops();
		defs << "  </linearGradient>\n";
	}
	return id;
}

} // namespace

bool writeString(const plot::ParsedPathDoc& doc, float paperWMm, float paperHMm,
				 std::string& outXml) {
	if (paperWMm <= 0.f) {
		paperWMm = doc.contentW > 0.f ? doc.contentW : 210.f;
	}
	if (paperHMm <= 0.f) {
		paperHMm = doc.contentH > 0.f ? doc.contentH : 297.f;
	}

	std::ostringstream defs;
	std::ostringstream body;
	int nextGradId = 0;
	// Cache: paintIndex + quantized bounds to emitted svg id (shared defs when possible).
	std::unordered_map<std::string, std::string> emitted;

	auto gradUrl = [&](int32_t paintIndex, const plot::PathBounds& b) -> std::string {
		if (paintIndex < 0 || paintIndex >= static_cast<int>(doc.paints.gradients.size())) {
			return {};
		}
		char key[64];
		std::snprintf(key, sizeof(key), "%d:%.2f:%.2f:%.2f:%.2f", paintIndex, b.minX, b.minY,
					  b.maxX, b.maxY);
		auto it = emitted.find(key);
		if (it != emitted.end()) {
			return it->second;
		}
		const std::string id = emitGradient(
			defs, doc.paints.gradients[static_cast<size_t>(paintIndex)], b, nextGradId);
		emitted[key] = id;
		return id;
	};

	for (const auto& layer : doc.layers) {
		body << "  <g id=\"" << esc(layer.name) << "\">\n";
		for (const auto& img : layer.images) {
			if (img.href.empty() || img.size.x <= 0.f || img.size.y <= 0.f) {
				continue;
			}
			body << "    <image href=\"" << esc(img.href) << "\" x=\"" << img.pos.x << "\" y=\""
				 << img.pos.y << "\" width=\"" << img.size.x << "\" height=\"" << img.size.y
				 << "\" preserveAspectRatio=\"none\"";
			if (img.opacity < 0.999f) {
				body << " opacity=\"" << img.opacity << "\"";
			}
			body << "/>\n";
		}
		const auto& bag = layer.paths;
		for (size_t i = 0; i < bag.paths.size(); ++i) {
			if (bag.isTravel(i) || bag.paths[i].empty()) {
				continue;
			}
			const plot::PathBounds b = plot::boundsOfPath(bag.paths[i]);
			body << "    <path d=\"" << pathD(bag.paths[i]) << "\"";

			const bool filled = bag.isFilled(i);
			const bool stroked = bag.isStroked(i);
			if (filled && bag.isEvenOdd(i)) {
				body << " fill-rule=\"evenodd\"";
			}
			if (filled) {
				const int32_t fp = bag.fillPaint(i);
				if (fp != ecs::kPaintSolid) {
					const std::string id = gradUrl(fp, b);
					if (!id.empty()) {
						body << " fill=\"url(#" << id << ")\"";
					} else {
						body << " fill=\""
							 << colorCss(i < bag.pathColors.size() ? bag.pathColors[i]
																   : layer.layerColor)
							 << "\"";
					}
				} else {
					const glm::vec4 c =
						i < bag.pathColors.size() ? bag.pathColors[i] : layer.layerColor;
					body << " fill=\"" << colorCss(c) << "\"";
				}
			} else {
				body << " fill=\"none\"";
			}

			if (stroked) {
				const float sw = bag.strokeWidth(i, 1.f);
				body << " stroke-width=\"" << sw << "\"";
				const int32_t sp = bag.strokePaint(i);
				if (sp != ecs::kPaintSolid) {
					const std::string id = gradUrl(sp, b);
					if (!id.empty()) {
						body << " stroke=\"url(#" << id << ")\"";
					} else {
						const glm::vec4 c = i < bag.pathStrokeColors.size()
												? bag.pathStrokeColors[i]
												: layer.layerColor;
						body << " stroke=\"" << colorCss(c) << "\"";
					}
				} else {
					const glm::vec4 c = i < bag.pathStrokeColors.size() ? bag.pathStrokeColors[i]
																		: layer.layerColor;
					body << " stroke=\"" << colorCss(c) << "\"";
				}
			} else {
				body << " stroke=\"none\"";
			}
			body << "/>\n";
		}
		body << "  </g>\n";
	}

	std::ostringstream root;
	root.setf(std::ios::fixed);
	root.precision(3);
	root << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		 << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << paperWMm << "mm\" height=\""
		 << paperHMm << "mm\" viewBox=\"0 0 " << paperWMm << " " << paperHMm << "\">\n";
	if (!defs.str().empty()) {
		root << "  <defs>\n" << defs.str() << "  </defs>\n";
	}
	root << body.str() << "</svg>\n";
	outXml = root.str();
	return true;
}

bool writeFile(const plot::ParsedPathDoc& doc, const std::string& path, float paperWMm,
			   float paperHMm) {
	std::string xml;
	if (!writeString(doc, paperWMm, paperHMm, xml)) {
		return false;
	}
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << xml;
	return static_cast<bool>(out);
}

} // namespace svg
} // namespace rigkit
