#include "SvgPathParser.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/glm.hpp>
#include "PathEllipticArc.h"

namespace rigkit {
namespace svg {
namespace {

struct Tokenizer {
	const char* p = nullptr;
	explicit Tokenizer(const std::string& s) : p(s.c_str()) {}

	void skip() {
		while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) {
			++p;
		}
	}

	bool done() {
		skip();
		return *p == '\0';
	}

	bool isCommand() {
		skip();
		const char c = *p;
		return std::isalpha(static_cast<unsigned char>(c)) != 0;
	}

	char takeCommand() {
		skip();
		return *p ? *p++ : '\0';
	}

	bool nextNumber(float& out) {
		skip();
		if (!*p) {
			return false;
		}
		char* end = nullptr;
		out = std::strtof(p, &end);
		if (end == p) {
			return false;
		}
		p = end;
		return true;
	}
};

} // namespace

bool parsePathD(const std::string& d, ecs::CPath& out) {
	out.commands.clear();
	Tokenizer tok(d);
	char cmd = '\0';
	glm::vec2 cur{0.f, 0.f};
	glm::vec2 start{0.f, 0.f};
	glm::vec2 lastC{0.f, 0.f}; // last cubic c2 or quad c1 (for S/T)
	enum class LastCurve : uint8_t { None, Cubic, Quad } lastCurve = LastCurve::None;
	using Cmd = ecs::CPath::Cmd;

	while (!tok.done()) {
		if (tok.isCommand()) {
			cmd = tok.takeCommand();
		}
		if (!cmd) {
			break;
		}

		const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
		const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

		auto absPt = [&](float x, float y) -> glm::vec2 {
			return rel ? cur + glm::vec2{x, y} : glm::vec2{x, y};
		};

		switch (op) {
		case 'M': {
			float x = 0.f, y = 0.f;
			if (!tok.nextNumber(x) || !tok.nextNumber(y)) {
				return false;
			}
			cur = absPt(x, y);
			start = cur;
			out.commands.push_back({Cmd::MoveTo, cur, {}, {}});
			lastCurve = LastCurve::None;
			cmd = rel ? 'l' : 'L';
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x) || !tok.nextNumber(y)) {
					break;
				}
				cur = absPt(x, y);
				out.commands.push_back({Cmd::LineTo, cur, {}, {}});
			}
			break;
		}
		case 'L': {
			float x = 0.f, y = 0.f;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x) || !tok.nextNumber(y)) {
					break;
				}
				cur = absPt(x, y);
				out.commands.push_back({Cmd::LineTo, cur, {}, {}});
				lastCurve = LastCurve::None;
			}
			break;
		}
		case 'H': {
			float x = 0.f;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x)) {
					break;
				}
				cur = rel ? glm::vec2{cur.x + x, cur.y} : glm::vec2{x, cur.y};
				out.commands.push_back({Cmd::LineTo, cur, {}, {}});
				lastCurve = LastCurve::None;
			}
			break;
		}
		case 'V': {
			float y = 0.f;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(y)) {
					break;
				}
				cur = rel ? glm::vec2{cur.x, cur.y + y} : glm::vec2{cur.x, y};
				out.commands.push_back({Cmd::LineTo, cur, {}, {}});
				lastCurve = LastCurve::None;
			}
			break;
		}
		case 'C': {
			float x1, y1, x2, y2, x, y;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x1) || !tok.nextNumber(y1) || !tok.nextNumber(x2) ||
					!tok.nextNumber(y2) || !tok.nextNumber(x) || !tok.nextNumber(y)) {
					break;
				}
				const glm::vec2 c1 = absPt(x1, y1);
				const glm::vec2 c2 = absPt(x2, y2);
				cur = absPt(x, y);
				out.commands.push_back({Cmd::CubicTo, cur, c1, c2});
				lastC = c2;
				lastCurve = LastCurve::Cubic;
			}
			break;
		}
		case 'S': {
			float x2, y2, x, y;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x2) || !tok.nextNumber(y2) || !tok.nextNumber(x) ||
					!tok.nextNumber(y)) {
					break;
				}
				const glm::vec2 c1 = (lastCurve == LastCurve::Cubic) ? (cur * 2.f - lastC) : cur;
				const glm::vec2 c2 = absPt(x2, y2);
				cur = absPt(x, y);
				out.commands.push_back({Cmd::CubicTo, cur, c1, c2});
				lastC = c2;
				lastCurve = LastCurve::Cubic;
			}
			break;
		}
		case 'Q': {
			float x1, y1, x, y;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x1) || !tok.nextNumber(y1) || !tok.nextNumber(x) ||
					!tok.nextNumber(y)) {
					break;
				}
				const glm::vec2 c1 = absPt(x1, y1);
				cur = absPt(x, y);
				out.commands.push_back({Cmd::QuadTo, cur, c1, {}});
				lastC = c1;
				lastCurve = LastCurve::Quad;
			}
			break;
		}
		case 'T': {
			float x, y;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(x) || !tok.nextNumber(y)) {
					break;
				}
				const glm::vec2 c1 = (lastCurve == LastCurve::Quad) ? (cur * 2.f - lastC) : cur;
				cur = absPt(x, y);
				out.commands.push_back({Cmd::QuadTo, cur, c1, {}});
				lastC = c1;
				lastCurve = LastCurve::Quad;
			}
			break;
		}
		case 'A': {
			float rx, ry, phi, large, sweep, x, y;
			while (!tok.done() && !tok.isCommand()) {
				if (!tok.nextNumber(rx) || !tok.nextNumber(ry) || !tok.nextNumber(phi) ||
					!tok.nextNumber(large) || !tok.nextNumber(sweep) || !tok.nextNumber(x) ||
					!tok.nextNumber(y)) {
					break;
				}
				const glm::vec2 end = absPt(x, y);
				if (std::abs(cur.x - end.x) < 1e-9f && std::abs(cur.y - end.y) < 1e-9f) {
					lastCurve = LastCurve::None;
					continue;
				}
				if (std::abs(rx) < 1e-9f || std::abs(ry) < 1e-9f) {
					cur = end;
					out.commands.push_back({Cmd::LineTo, cur, {}, {}});
				} else {
					out.commands.push_back(
						path::makeArcTo(end, rx, ry, phi, large != 0.f, sweep != 0.f));
					cur = end;
				}
				lastCurve = LastCurve::None;
			}
			break;
		}
		case 'Z':
			out.commands.push_back({Cmd::Close, {}, {}, {}});
			cur = start;
			lastCurve = LastCurve::None;
			break;
		default:
			return !out.commands.empty();
		}
	}
	return !out.commands.empty();
}

} // namespace svg
} // namespace rigkit
