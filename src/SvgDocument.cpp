#include "SvgDocument.h"

#include "core/TypeJson.h"

#include <unordered_map>
#include <unordered_set>

namespace rigkit {
namespace svg {
namespace {

using json = nlohmann::ordered_json;

json vec2Json(const glm::vec2& p) {
	return json::array({p.x, p.y});
}

json vec4Json(const glm::vec4& c) {
	return json::array({c.r, c.g, c.b, c.a});
}

glm::vec2 vec2From(const json& j, glm::vec2 fallback = {}) {
	if (!j.is_array() || j.size() < 2) {
		return fallback;
	}
	return {j[0].get<float>(), j[1].get<float>()};
}

glm::vec4 vec4From(const json& j, glm::vec4 fallback = {0.f, 0.f, 0.f, 1.f}) {
	if (!j.is_array() || j.size() < 3) {
		return fallback;
	}
	return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(),
			j.size() > 3 ? j[3].get<float>() : 1.f};
}

json pathCommandsJson(const ecs::CPath& path) {
	json commands = json::array();
	for (const auto& cmd : path.commands) {
		json row;
		using Cmd = ecs::CPath::Cmd;
		switch (cmd.type) {
		case Cmd::LineTo:
			row["type"] = "line-to";
			row["point"] = vec2Json(cmd.p);
			break;
		case Cmd::CubicTo:
			row["type"] = "cubic-to";
			row["point"] = vec2Json(cmd.p);
			row["control1"] = vec2Json(cmd.c1);
			row["control2"] = vec2Json(cmd.c2);
			break;
		case Cmd::QuadTo:
			row["type"] = "quad-to";
			row["point"] = vec2Json(cmd.p);
			row["control1"] = vec2Json(cmd.c1);
			break;
		case Cmd::Close:
			row["type"] = "close";
			break;
		case Cmd::ArcTo:
			row["type"] = "arc-to";
			row["point"] = vec2Json(cmd.p);
			row["radiusX"] = cmd.radii.x;
			row["radiusY"] = cmd.radii.y;
			row["xAxisRotationDegrees"] = cmd.xRotDeg;
			if (cmd.largeArc) {
				row["largeArc"] = true;
			}
			if (cmd.sweep) {
				row["sweep"] = true;
			}
			break;
		default:
			row["type"] = "move-to";
			row["point"] = vec2Json(cmd.p);
			break;
		}
		commands.push_back(std::move(row));
	}
	return commands;
}

ecs::CPath pathFromCommands(const json& path) {
	ecs::CPath out;
	if (!path.contains("commands") || !path["commands"].is_array()) {
		return out;
	}
	for (const auto& cmd : path["commands"]) {
		const std::string type = cmd.value("type", std::string("move-to"));
		ecs::CPath::Command row;
		if (type == "line-to") {
			row.type = ecs::CPath::Cmd::LineTo;
			row.p = vec2From(cmd.value("point", json::array()));
		} else if (type == "cubic-to") {
			row.type = ecs::CPath::Cmd::CubicTo;
			row.p = vec2From(cmd.value("point", json::array()));
			row.c1 = vec2From(cmd.value("control1", json::array()));
			row.c2 = vec2From(cmd.value("control2", json::array()));
		} else if (type == "quad-to") {
			row.type = ecs::CPath::Cmd::QuadTo;
			row.p = vec2From(cmd.value("point", json::array()));
			row.c1 = vec2From(cmd.value("control1", json::array()));
		} else if (type == "close") {
			row.type = ecs::CPath::Cmd::Close;
		} else if (type == "arc-to") {
			row.type = ecs::CPath::Cmd::ArcTo;
			row.p = vec2From(cmd.value("point", json::array()));
			row.radii.x = cmd.value("radiusX", 0.f);
			row.radii.y = cmd.value("radiusY", 0.f);
			row.xRotDeg = cmd.value("xAxisRotationDegrees", 0.f);
			row.largeArc = cmd.value("largeArc", false);
			row.sweep = cmd.value("sweep", false);
		} else {
			row.type = ecs::CPath::Cmd::MoveTo;
			row.p = vec2From(cmd.value("point", json::array()));
		}
		out.commands.push_back(row);
	}
	out.evenOdd = path.value("fillRule", std::string("nonzero")) == "evenodd";
	return out;
}

std::string pathEntityId(const ecs::CPaths& bag, size_t i, size_t layerIndex) {
	if (i < bag.pathId.size() && !bag.pathId[i].empty()) {
		return bag.pathId[i];
	}
	return "path." + std::to_string(layerIndex) + "." + std::to_string(i);
}

json named(const std::string& id, const std::string& name) {
	return {{"name", name.empty() ? id : name}, {"stableId", id}};
}

json fillStroke(bool filled, const glm::vec4& fill, bool stroked, const glm::vec4& stroke,
				float width) {
	json p;
	p["hasFill"] = filled;
	p["hasStroke"] = stroked;
	if (filled) {
		p["fillRgba"] = vec4Json(fill);
	}
	if (stroked) {
		p["strokeRgba"] = vec4Json(stroke);
		p["strokeWidth"] = width;
	}
	return p;
}

const char* anchorName(ecs::TextItem::Anchor a) {
	switch (a) {
	case ecs::TextItem::Anchor::Middle:
		return "middle";
	case ecs::TextItem::Anchor::End:
		return "end";
	default:
		return "start";
	}
}

ecs::TextItem::Anchor anchorFrom(const std::string& s) {
	if (s == "middle") {
		return ecs::TextItem::Anchor::Middle;
	}
	if (s == "end") {
		return ecs::TextItem::Anchor::End;
	}
	return ecs::TextItem::Anchor::Start;
}

} // namespace

bool writeDocument(const plot::ParsedPathDoc& doc, std::string& outJson) {
	json root;
	root["rig"] = "0.40.0";
	root["document"] = {{"title", "SVG"}, {"defaultUnit", "px"}};
	json entities = json::array();

	for (size_t fi = 0; fi < doc.fontFaces.size(); ++fi) {
		const auto& face = doc.fontFaces[fi];
		const std::string id = face.family.empty() ? ("font." + std::to_string(fi)) : face.family;
		json comps;
		comps["rig.meta.named"] = named(id, face.family);
		if (!face.href.empty()) {
			comps["rig.media.asset_ref"] = {{"kind", "font"}, {"path", face.href}};
		}
		comps["rig.media.font_face"] = {{"family", face.family}, {"asset", id}};
		entities.push_back({{"id", id}, {"components", std::move(comps)}});
	}

	for (const auto& clip : doc.clips) {
		if (clip.id.empty()) {
			continue;
		}
		json comps;
		comps["rig.meta.named"] = named(clip.id, clip.id);
		json path;
		path["commands"] = pathCommandsJson(clip.path);
		if (clip.path.evenOdd) {
			path["fillRule"] = "evenodd";
		}
		comps["rig.geometry.path"] = std::move(path);
		entities.push_back({{"id", clip.id}, {"components", std::move(comps)}});
	}

	for (size_t li = 0; li < doc.layers.size(); ++li) {
		const auto& layer = doc.layers[li];
		const auto& bag = layer.paths;
		for (size_t i = 0; i < bag.paths.size(); ++i) {
			if (bag.paths[i].empty()) {
				continue;
			}
			const std::string id = pathEntityId(bag, i, li);
			json comps;
			comps["rig.meta.named"] = named(id, id);
			json path;
			path["commands"] = pathCommandsJson(bag.paths[i]);
			if (bag.isEvenOdd(i) || bag.paths[i].evenOdd) {
				path["fillRule"] = "evenodd";
			}
			comps["rig.geometry.path"] = std::move(path);
			const glm::vec4 fill =
				i < bag.pathColors.size() ? bag.pathColors[i] : layer.layerColor;
			const glm::vec4 stroke = i < bag.pathStrokeColors.size() ? bag.pathStrokeColors[i]
																	 : layer.layerColor;
			comps["rig.paint.fill_stroke"] =
				fillStroke(bag.isFilled(i), fill, bag.isStroked(i), stroke, bag.strokeWidth(i));
			if (i < bag.pathClipId.size() && !bag.pathClipId[i].empty()) {
				comps["rig.paint.clip"] = {{"path", bag.pathClipId[i]}};
			}
			entities.push_back({{"id", id}, {"components", std::move(comps)}});
		}
		for (size_t ti = 0; ti < layer.texts.items.size(); ++ti) {
			const auto& t = layer.texts.items[ti];
			if (t.text.empty()) {
				continue;
			}
			const std::string id = "text." + std::to_string(li) + "." + std::to_string(ti);
			json comps;
			comps["rig.meta.named"] = named(id, t.text);
			json run;
			run["text"] = t.text;
			run["fontSize"] = t.size;
			run["anchor"] = anchorName(t.anchor);
			if (t.tracking != 0.f) {
				run["tracking"] = t.tracking;
			}
			for (const auto& face : doc.fontFaces) {
				if (!face.family.empty() && t.fontPath == face.family) {
					run["font"] = face.family;
					break;
				}
			}
			comps["rig.media.text"] = std::move(run);
			comps["rig.spatial.transform"] = {{"position", json::array({t.baseline.x, t.baseline.y, 0.f})}};
			comps["rig.paint.fill_stroke"] =
				fillStroke(true, t.rgba, false, {0.f, 0.f, 0.f, 1.f}, 1.f);
			std::string href = t.pathHref;
			if (href.empty() && t.pathIndex >= 0) {
				href = pathEntityId(bag, static_cast<size_t>(t.pathIndex), li);
			}
			if (!href.empty()) {
				json tp;
				tp["path"] = href;
				if (t.startOffset != 0.f) {
					tp["startOffset"] = t.startOffset;
				}
				if (t.startOffsetMode == ecs::TextItem::OffsetMode::Ratio) {
					tp["startOffsetMode"] = "ratio";
				}
				comps["rig.media.text_path"] = std::move(tp);
			}
			if (!t.clipId.empty()) {
				comps["rig.paint.clip"] = {{"path", t.clipId}};
			}
			entities.push_back({{"id", id}, {"components", std::move(comps)}});
		}
	}

	root["entities"] = std::move(entities);
	outJson = root.dump(2);
	outJson.push_back('\n');
	return true;
}

bool readDocument(const std::string& jsonText, plot::ParsedPathDoc& out) {
	out = {};
	json root;
	try {
		root = json::parse(jsonText);
	} catch (...) {
		return false;
	}
	if (!root.contains("entities") || !root["entities"].is_array()) {
		return false;
	}

	std::unordered_set<std::string> clipIds;
	for (const auto& ent : root["entities"]) {
		const auto& comps = ent.value("components", json::object());
		if (comps.contains("rig.paint.clip")) {
			const std::string path = comps["rig.paint.clip"].value("path", std::string());
			if (!path.empty()) {
				clipIds.insert(path);
			}
		}
	}

	auto& layer = out.layers.emplace_back();
	layer.name = "SVG";
	std::unordered_map<std::string, int> pathIndex;

	for (const auto& ent : root["entities"]) {
		const std::string id = ent.value("id", std::string());
		const auto& comps = ent.value("components", json::object());
		if (comps.contains("rig.media.font_face")) {
			plot::ParsedPathDoc::FontFace face;
			face.family = comps["rig.media.font_face"].value("family", std::string());
			if (comps.contains("rig.media.asset_ref")) {
				face.href = comps["rig.media.asset_ref"].value(
					"path", comps["rig.media.asset_ref"].value("href", std::string()));
			}
			if (!face.family.empty()) {
				out.fontFaces.push_back(std::move(face));
			}
			continue;
		}
		if (!comps.contains("rig.geometry.path")) {
			continue;
		}
		ecs::CPath path = pathFromCommands(comps["rig.geometry.path"]);
		const bool isClip = clipIds.count(id) != 0 && !comps.contains("rig.paint.fill_stroke");
		if (isClip) {
			out.clips.push_back({id, std::move(path)});
			continue;
		}
		auto& bag = layer.paths;
		const size_t i = bag.paths.size();
		bag.paths.push_back(std::move(path));
		bag.pathId.resize(i + 1);
		bag.pathId[i] = id;
		bag.pathFilled.resize(i + 1, 0);
		bag.pathStroked.resize(i + 1, 0);
		bag.pathColors.resize(i + 1, glm::vec4{0, 0, 0, 1});
		bag.pathStrokeColors.resize(i + 1, glm::vec4{0, 0, 0, 1});
		bag.pathStrokeWidths.resize(i + 1, 1.f);
		bag.pathFillRule.resize(i + 1, 0);
		bag.pathClipId.resize(i + 1);
		bag.pathFillRule[i] = bag.paths[i].evenOdd ? 1 : 0;
		if (comps.contains("rig.paint.fill_stroke")) {
			const auto& fs = comps["rig.paint.fill_stroke"];
			bag.pathFilled[i] = fs.value("hasFill", fs.contains("fillRgba")) ? 1 : 0;
			bag.pathStroked[i] = fs.value("hasStroke", fs.contains("strokeRgba")) ? 1 : 0;
			if (fs.contains("fillRgba")) {
				bag.pathColors[i] = vec4From(fs["fillRgba"]);
			}
			if (fs.contains("strokeRgba")) {
				bag.pathStrokeColors[i] = vec4From(fs["strokeRgba"]);
			}
			bag.pathStrokeWidths[i] = fs.value("strokeWidth", 1.f);
		} else {
			bag.pathFilled[i] = 1;
		}
		if (comps.contains("rig.paint.clip")) {
			bag.pathClipId[i] = comps["rig.paint.clip"].value("path", std::string());
		}
		pathIndex[id] = static_cast<int>(i);
	}

	for (const auto& ent : root["entities"]) {
		const auto& comps = ent.value("components", json::object());
		if (!comps.contains("rig.media.text")) {
			continue;
		}
		const auto& run = comps["rig.media.text"];
		ecs::TextItem item;
		item.text = run.value("text", std::string());
		item.size = run.value("fontSize", 16.f);
		item.tracking = run.value("tracking", 0.f);
		item.anchor = anchorFrom(run.value("anchor", std::string("start")));
		item.fontPath = run.value("font", std::string());
		if (comps.contains("rig.spatial.transform") &&
			comps["rig.spatial.transform"].contains("position")) {
			const auto pos = vec2From(comps["rig.spatial.transform"]["position"]);
			item.baseline = pos;
		}
		if (comps.contains("rig.paint.fill_stroke") &&
			comps["rig.paint.fill_stroke"].contains("fillRgba")) {
			item.rgba = vec4From(comps["rig.paint.fill_stroke"]["fillRgba"]);
		}
		if (comps.contains("rig.media.text_path")) {
			const auto& tp = comps["rig.media.text_path"];
			item.pathHref = tp.value("path", std::string());
			item.startOffset = tp.value("startOffset", 0.f);
			if (tp.value("startOffsetMode", std::string("length")) == "ratio") {
				item.startOffsetMode = ecs::TextItem::OffsetMode::Ratio;
			}
			auto it = pathIndex.find(item.pathHref);
			if (it != pathIndex.end()) {
				item.pathIndex = it->second;
			}
		}
		if (comps.contains("rig.paint.clip")) {
			item.clipId = comps["rig.paint.clip"].value("path", std::string());
		}
		if (!item.text.empty()) {
			layer.texts.items.push_back(std::move(item));
		}
	}

	return !out.layers.empty() || !out.clips.empty() || !out.fontFaces.empty();
}

} // namespace svg
} // namespace rigkit
