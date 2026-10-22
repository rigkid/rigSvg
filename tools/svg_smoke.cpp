#include "SvgDocument.h"
#include "SvgLoader.h"
#include "SvgPathParser.h"
#include "SvgThumbnail.h"
#include "SvgWriter.h"

#include "core/util/AppPaths.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
	// Arc stays ArcTo; smooth S/T still expand.
	{
		rigkit::ecs::CPath arcPath;
		if (!rigkit::svg::parsePathD("M20 40 A20 20 0 0 1 60 40", arcPath) ||
			arcPath.commands.size() != 2) {
			std::printf("svg_smoke FAIL arc parse cmds=%zu\n", arcPath.commands.size());
			return 1;
		}
		if (arcPath.commands[1].type != rigkit::ecs::CPath::Cmd::ArcTo ||
			std::abs(arcPath.commands[1].radii.x - 20.f) > 1e-3f || !arcPath.commands[1].sweep) {
			std::printf("svg_smoke FAIL arc not retained as ArcTo\n");
			return 1;
		}
		std::string arcXml;
		{
			rigkit::plot::ParsedPathDoc adoc;
			rigkit::plot::PathSourceLayer layer;
			layer.paths.paths.push_back(arcPath);
			layer.paths.pathFilled.push_back(0);
			layer.paths.pathStroked.push_back(1);
			layer.paths.pathStrokeColors.push_back({0.f, 0.f, 0.f, 1.f});
			adoc.layers.push_back(std::move(layer));
			if (!rigkit::svg::writeString(adoc, 120.f, 80.f, arcXml) ||
				arcXml.find("A20") == std::string::npos) {
				std::printf("svg_smoke FAIL arc export lost A\n");
				return 1;
			}
		}
		rigkit::ecs::CPath smooth;
		if (!rigkit::svg::parsePathD("M10 70 C20 50 40 50 50 70 S80 90 90 70", smooth) ||
			smooth.commands.size() < 3) {
			std::printf("svg_smoke FAIL smooth S parse\n");
			return 1;
		}
		rigkit::ecs::CPath tpath;
		if (!rigkit::svg::parsePathD("M70 20 Q85 5 100 20 T120 20", tpath) ||
			tpath.commands.size() < 3) {
			std::printf("svg_smoke FAIL smooth T parse\n");
			return 1;
		}
	}

	fs::path fixture;
	if (argc > 1) {
		fixture = argv[1];
	} else {
		fixture = fs::path(__FILE__).parent_path() / "fixtures" / "roundtrip-fills.svg";
	}

	rigkit::plot::ParsedPathDoc doc;
	if (!rigkit::svg::loadFile(fixture.string(), doc) || doc.empty()) {
		std::printf("svg_smoke FAIL parse '%s'\n", fixture.string().c_str());
		return 1;
	}

	const fs::path arcsFixture = fs::path(__FILE__).parent_path() / "fixtures" / "arcs-smooth.svg";
	if (fs::exists(arcsFixture)) {
		rigkit::plot::ParsedPathDoc arcs;
		if (!rigkit::svg::loadFile(arcsFixture.string(), arcs) || arcs.empty()) {
			std::printf("svg_smoke FAIL arcs-smooth.svg\n");
			return 1;
		}
	}

	const fs::path tiger =
		fs::path(__FILE__).parent_path().parent_path() / "examples" / "svg" / "data" / "tiger.svg";
	if (fs::exists(tiger)) {
		rigkit::plot::ParsedPathDoc tigerDoc;
		if (!rigkit::svg::loadFile(tiger.string(), tigerDoc) || tigerDoc.empty()) {
			std::printf("svg_smoke FAIL tiger.svg parse\n");
			return 1;
		}
		size_t tigerPaths = 0;
		float maxStroke = 0.f;
		for (const auto& layer : tigerDoc.layers) {
			tigerPaths += layer.paths.paths.size();
			for (size_t i = 0; i < layer.paths.paths.size(); ++i) {
				maxStroke = std::max(maxStroke, layer.paths.strokeWidth(i));
			}
		}
		if (tigerPaths < 100) {
			std::printf("svg_smoke FAIL tiger.svg paths=%zu\n", tigerPaths);
			return 1;
		}
		// Ghostscript tiger stroke-width is 1.892 in pre-CTM space (~0.07 after scale).
		if (maxStroke > 1.5f) {
			std::printf("svg_smoke FAIL tiger.svg stroke-width not CTM-scaled (max=%.3f)\n",
						maxStroke);
			return 1;
		}
		std::printf("svg_smoke tiger ok paths=%zu layers=%zu maxStroke=%.3f\n", tigerPaths,
					tigerDoc.layers.size(), maxStroke);
	}

	size_t paths = 0;
	size_t filled = 0;
	size_t stroked = 0;
	size_t fillGrads = 0;
	size_t strokeGrads = 0;
	for (const auto& layer : doc.layers) {
		const auto& bag = layer.paths;
		paths += bag.paths.size();
		for (size_t i = 0; i < bag.paths.size(); ++i) {
			if (bag.isFilled(i)) {
				++filled;
			}
			if (bag.isStroked(i)) {
				++stroked;
			}
			if (bag.fillPaint(i) != rigkit::ecs::kPaintSolid) {
				++fillGrads;
			}
			if (bag.strokePaint(i) != rigkit::ecs::kPaintSolid) {
				++strokeGrads;
			}
		}
	}

	if (paths < 8 || doc.paints.gradients.size() < 3 || fillGrads < 2 || strokeGrads < 1) {
		std::printf("svg_smoke FAIL counts paths=%zu paints=%zu fillGrads=%zu strokeGrads=%zu "
					"filled=%zu stroked=%zu\n",
					paths, doc.paints.gradients.size(), fillGrads, strokeGrads, filled, stroked);
		return 1;
	}

	const fs::path outPath = fs::temp_directory_path() / "rigSvg_roundtrip.svg";
	if (!rigkit::svg::writeFile(doc, outPath.string(), 210.f, 297.f)) {
		std::printf("svg_smoke FAIL export\n");
		return 1;
	}

	rigkit::plot::ParsedPathDoc again;
	if (!rigkit::svg::loadFile(outPath.string(), again) || again.layers.empty()) {
		std::printf("svg_smoke FAIL re-import\n");
		return 1;
	}
	size_t paths2 = 0;
	for (const auto& layer : again.layers) {
		paths2 += layer.paths.paths.size();
	}
	if (paths2 < paths) {
		std::printf("svg_smoke FAIL re-import path count %zu < %zu\n", paths2, paths);
		return 1;
	}
	if (again.paints.gradients.empty()) {
		std::printf("svg_smoke FAIL re-import lost gradients\n");
		return 1;
	}

	// Group fill/stroke inheritance (Ghostscript Tiger style: paint on <g>, not <path>).
	{
		const fs::path inheritFix =
			fs::path(__FILE__).parent_path() / "fixtures" / "group-fill-inherit.svg";
		rigkit::plot::ParsedPathDoc inherit;
		if (!rigkit::svg::loadFile(inheritFix.string(), inherit) || inherit.layers.empty()) {
			std::printf("svg_smoke FAIL group-fill-inherit.svg load\n");
			return 1;
		}
		const auto& bag = inherit.layers.front().paths;
		if (bag.paths.size() < 3 || bag.pathColors.size() < 3) {
			std::printf("svg_smoke FAIL inherit path count %zu layers=%zu\n", bag.paths.size(),
						inherit.layers.size());
			return 1;
		}
		// Not named "near": windows.h defines near/far as legacy macros.
		auto nearRgb = [](const glm::vec4& c, float r, float g, float b) {
			return std::abs(c.r - r) < 0.05f && std::abs(c.g - g) < 0.05f &&
				   std::abs(c.b - b) < 0.05f;
		};
		if (!bag.isFilled(0) || !nearRgb(bag.pathColors[0], 1.f, 1.f, 1.f) || !bag.isStroked(0)) {
			std::printf("svg_smoke FAIL inherit white+stroke\n");
			return 1;
		}
		if (!bag.isFilled(1) ||
			!nearRgb(bag.pathColors[1], 0xE0 / 255.f, 0x70 / 255.f, 0x20 / 255.f) ||
			bag.isStroked(1)) {
			std::printf("svg_smoke FAIL inherit orange stroke:none\n");
			return 1;
		}
		if (!bag.isFilled(2) || !nearRgb(bag.pathColors[2], 0.f, 0.f, 0.f) || !bag.isStroked(2)) {
			std::printf("svg_smoke FAIL inherit black+stroke\n");
			return 1;
		}
	}

	// fill-rule="evenodd": attribute, style, and group inheritance must all
	// set the per-path flag, and it must survive a write/re-import round trip.
	{
		const std::string svg =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
			"<path d=\"M10 10 H40 V40 H10 Z M20 20 H30 V30 H20 Z\" fill-rule=\"evenodd\"/>"
			"<path d=\"M50 10 H80 V40 H50 Z\" style=\"fill:#f00;fill-rule:evenodd\"/>"
			"<g fill-rule=\"evenodd\"><path d=\"M10 50 H40 V80 H10 Z\"/>"
			"<path d=\"M50 50 H80 V80 H50 Z\" fill-rule=\"nonzero\"/></g>"
			"</svg>";
		rigkit::plot::ParsedPathDoc fr;
		if (!rigkit::svg::loadString(svg, fr) || fr.layers.empty()) {
			std::printf("svg_smoke FAIL fill-rule load\n");
			return 1;
		}
		size_t evenOdd = 0;
		size_t total = 0;
		for (const auto& layer : fr.layers) {
			const auto& bag = layer.paths;
			total += bag.paths.size();
			for (size_t i = 0; i < bag.paths.size(); ++i) {
				if (bag.isEvenOdd(i)) {
					++evenOdd;
				}
			}
		}
		if (total != 4 || evenOdd != 3) {
			std::printf("svg_smoke FAIL fill-rule flags paths=%zu evenOdd=%zu (want 4/3)\n", total,
						evenOdd);
			return 1;
		}
		std::string xml;
		if (!rigkit::svg::writeString(fr, 100.f, 100.f, xml) ||
			xml.find("fill-rule=\"evenodd\"") == std::string::npos) {
			std::printf("svg_smoke FAIL fill-rule not written\n");
			return 1;
		}
		rigkit::plot::ParsedPathDoc fr2;
		size_t evenOdd2 = 0;
		if (!rigkit::svg::loadString(xml, fr2)) {
			std::printf("svg_smoke FAIL fill-rule re-import\n");
			return 1;
		}
		for (const auto& layer : fr2.layers) {
			const auto& bag = layer.paths;
			for (size_t i = 0; i < bag.paths.size(); ++i) {
				if (bag.isEvenOdd(i)) {
					++evenOdd2;
				}
			}
		}
		if (evenOdd2 != evenOdd) {
			std::printf("svg_smoke FAIL fill-rule roundtrip %zu != %zu\n", evenOdd2, evenOdd);
			return 1;
		}
	}

	// CorelDRAW: fill-rule:evenodd on the root <svg> style, CSS class on the
	// path, compound subpaths with no per-path fill-rule. Child nonzero wins.
	{
		const std::string corel =
			"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\" "
			"style=\"shape-rendering:geometricPrecision; fill-rule:evenodd; clip-rule:evenodd\">"
			"<defs><style type=\"text/css\"><![CDATA[.fil0 {fill:#39B54A}]]></style></defs>"
			"<g><path class=\"fil0\" d=\"M10 10 H90 V90 H10 Z M30 30 H70 V70 H30 Z\"/></g>"
			"<path d=\"M0 0 H10 V10 H0 Z\" fill-rule=\"nonzero\"/>"
			"</svg>";
		rigkit::plot::ParsedPathDoc cd;
		if (!rigkit::svg::loadString(corel, cd) || cd.layers.empty()) {
			std::printf("svg_smoke FAIL corel fill-rule load\n");
			return 1;
		}
		size_t paths = 0;
		size_t evenOdd = 0;
		for (const auto& layer : cd.layers) {
			const auto& bag = layer.paths;
			paths += bag.paths.size();
			for (size_t i = 0; i < bag.paths.size(); ++i) {
				if (bag.isEvenOdd(i)) {
					++evenOdd;
				}
			}
		}
		if (paths != 2 || evenOdd != 1) {
			std::printf("svg_smoke FAIL corel fill-rule paths=%zu evenOdd=%zu (want 2/1)\n", paths,
						evenOdd);
			return 1;
		}
	}

	// Dawn Hummingbird showcase: layered groups, gradients, diverse fills,
	// and evenodd compound paths (sun ring + flower center).
	{
		const fs::path showcase =
			fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
			"examples" / "svgEditor" / "data" / "showcase.svg";
		if (fs::exists(showcase)) {
			rigkit::plot::ParsedPathDoc sdoc;
			if (!rigkit::svg::loadFile(showcase.string(), sdoc) || sdoc.layers.empty()) {
				std::printf("svg_smoke FAIL showcase load\n");
				return 1;
			}
			size_t pathsS = 0;
			size_t filledS = 0;
			size_t evenOddS = 0;
			size_t uniqueRgb = 0;
			std::vector<uint32_t> seen;
			seen.reserve(64);
			for (const auto& layer : sdoc.layers) {
				const auto& bag = layer.paths;
				pathsS += bag.paths.size();
				for (size_t i = 0; i < bag.paths.size(); ++i) {
					if (bag.isEvenOdd(i)) {
						++evenOddS;
					}
					if (!bag.isFilled(i) || i >= bag.pathColors.size()) {
						continue;
					}
					++filledS;
					const auto& c = bag.pathColors[i];
					const uint32_t key = (static_cast<uint32_t>(c.r * 255.f) << 16) |
										 (static_cast<uint32_t>(c.g * 255.f) << 8) |
										 static_cast<uint32_t>(c.b * 255.f);
					bool found = false;
					for (uint32_t s : seen) {
						if (s == key) {
							found = true;
							break;
						}
					}
					if (!found) {
						seen.push_back(key);
						++uniqueRgb;
					}
				}
			}
			if (pathsS < 40 || filledS < 30 || evenOddS < 2 || uniqueRgb < 10 ||
				sdoc.layers.size() != 5) {
				std::printf("svg_smoke FAIL showcase paths=%zu filled=%zu evenOdd=%zu colors=%zu "
							"layers=%zu\n",
							pathsS, filledS, evenOddS, uniqueRgb, sdoc.layers.size());
				return 1;
			}
			std::printf("svg_smoke showcase ok paths=%zu filled=%zu evenOdd=%zu colors=%zu "
						"layers=%zu\n",
						pathsS, filledS, evenOddS, uniqueRgb, sdoc.layers.size());
		}
	}

	// OkLab densify + intensity clamp: extra stops, channels stay in 0-1.
	{
		rigkit::plot::ParsedPathDoc gdoc;
		rigkit::ecs::CPath box;
		using Cmd = rigkit::ecs::CPath::Cmd;
		box.commands.push_back({Cmd::MoveTo, {0.f, 0.f}, {}, {}});
		box.commands.push_back({Cmd::LineTo, {10.f, 0.f}, {}, {}});
		box.commands.push_back({Cmd::LineTo, {10.f, 10.f}, {}, {}});
		box.commands.push_back({Cmd::LineTo, {0.f, 10.f}, {}, {}});
		box.commands.push_back({Cmd::Close, {}, {}, {}});
		rigkit::plot::PathSourceLayer layer;
		layer.paths.paths.push_back(std::move(box));
		layer.paths.pathFilled.push_back(1);
		layer.paths.pathFillPaint.push_back(0);
		gdoc.layers.push_back(std::move(layer));
		rigkit::ecs::CGradient g;
		g.interp = rigkit::ecs::CGradient::Interp::OkLab;
		g.stops.push_back({0.f, {1.f, 0.f, 0.f, 1.f}, 3.f});
		g.stops.push_back({1.f, {0.f, 0.f, 1.f, 1.f}, 1.f});
		gdoc.paints.gradients.push_back(std::move(g));
		std::string xml;
		if (!rigkit::svg::writeString(gdoc, 10.f, 10.f, xml)) {
			std::printf("svg_smoke FAIL oklab write\n");
			return 1;
		}
		size_t stops = 0;
		for (size_t i = 0; (i = xml.find("<stop", i)) != std::string::npos; ++i) {
			++stops;
		}
		if (stops < 4) {
			std::printf("svg_smoke FAIL oklab densify stops=%zu\n", stops);
			return 1;
		}
		if (xml.find("rgba(") != std::string::npos) {
			std::printf("svg_smoke FAIL oklab intensity not clamped to opaque sRGB\n");
			return 1;
		}
	}

	// SVG > document > SVG: arc flags, text, textPath, clip, and font-face.
	{
		const fs::path round = fs::path(__FILE__).parent_path() / "fixtures" / "document-roundtrip.svg";
		rigkit::plot::ParsedPathDoc src;
		if (!rigkit::svg::loadFile(round.string(), src)) {
			std::printf("svg_smoke FAIL document fixture load\n");
			return 1;
		}
		bool sawArc = false;
		bool sawHello = false;
		bool sawAlong = false;
		bool sawClip = false;
		bool sawFace = false;
		for (const auto& layer : src.layers) {
			for (const auto& p : layer.paths.paths) {
				for (const auto& cmd : p.commands) {
					if (cmd.type == rigkit::ecs::CPath::Cmd::ArcTo && cmd.sweep && !cmd.largeArc) {
						sawArc = true;
					}
				}
			}
			for (const auto& t : layer.texts.items) {
				if (t.text == "Hello") {
					sawHello = true;
				}
				if (t.text == "Along" && (!t.pathHref.empty() || t.pathIndex >= 0)) {
					sawAlong = true;
				}
			}
			for (const auto& id : layer.paths.pathClipId) {
				if (id == "hole") {
					sawClip = true;
				}
			}
		}
		for (const auto& clip : src.clips) {
			if (clip.id == "hole") {
				sawClip = true;
			}
		}
		for (const auto& face : src.fontFaces) {
			if (face.family == "Roboto") {
				sawFace = true;
			}
		}
		if (!sawArc || !sawHello || !sawAlong || !sawClip || !sawFace) {
			std::printf("svg_smoke FAIL document load A=%d Hello=%d Along=%d clip=%d face=%d\n",
						sawArc, sawHello, sawAlong, sawClip, sawFace);
			return 1;
		}
		std::string json;
		if (!rigkit::svg::writeDocument(src, json) || json.find("arc-to") == std::string::npos ||
			json.find("Hello") == std::string::npos || json.find("hole") == std::string::npos ||
			json.find("Roboto") == std::string::npos ||
			json.find("text_path") == std::string::npos) {
			std::printf("svg_smoke FAIL document write\n");
			return 1;
		}
		{
			const fs::path jsonOut = fs::temp_directory_path() / "rigSvg_document.json";
			std::FILE* dump = std::fopen(jsonOut.string().c_str(), "wb");
			if (dump) {
				std::fputs(json.c_str(), dump);
				std::fclose(dump);
			}
		}
		rigkit::plot::ParsedPathDoc mid;
		if (!rigkit::svg::readDocument(json, mid)) {
			std::printf("svg_smoke FAIL document read\n");
			return 1;
		}
		std::string xml;
		if (!rigkit::svg::writeString(mid, 240.f, 120.f, xml) || xml.find("A20") == std::string::npos ||
			xml.find("Hello") == std::string::npos || xml.find("hole") == std::string::npos ||
			xml.find("Roboto") == std::string::npos) {
			std::printf("svg_smoke FAIL document SVG write\n");
			return 1;
		}
		std::printf("svg_smoke document roundtrip ok\n");
	}

	// Icon tile must draw <text> even when the family name is not installed.
	{
		AppPaths::init(argc, argv);
		const fs::path textSvg = fs::temp_directory_path() / "rigSvg_text_thumb.svg";
		{
			std::FILE* out = std::fopen(textSvg.string().c_str(), "wb");
			if (!out) {
				std::printf("svg_smoke FAIL text thumb open\n");
				return 1;
			}
			const char* xml =
				"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"80\" "
				"viewBox=\"0 0 240 80\">"
				"<text x=\"8\" y=\"58\" font-size=\"48\" fill=\"#000\" "
				"font-family=\"NotARealFont\">Hello</text>"
				"</svg>";
			std::fputs(xml, out);
			std::fclose(out);
		}
		int tw = 0;
		int th = 0;
		std::vector<std::uint8_t> pixels;
		if (!rigkit::svg::rasterizeFileThumbnail(textSvg.string(), 80, tw, th, pixels) || tw < 1 ||
			th < 1) {
			std::printf("svg_smoke FAIL text thumb raster\n");
			return 1;
		}
		size_t ink = 0;
		for (size_t i = 3; i < pixels.size(); i += 4) {
			if (pixels[i] > 16) {
				++ink;
			}
		}
		if (ink < 40) {
			std::printf("svg_smoke FAIL text thumb ink=%zu (Roboto or a system face missing?)\n",
						ink);
			return 1;
		}
		std::printf("svg_smoke text thumb ok %dx%d ink=%zu\n", tw, th, ink);
	}

	std::printf("svg_smoke ok paths=%zu paints=%zu fillGrads=%zu strokeGrads=%zu roundtrip=%zu\n",
				paths, doc.paints.gradients.size(), fillGrads, strokeGrads,
				again.paints.gradients.size());
	return 0;
}
