#include "rigSvg.h"

#include <spdlog/spdlog.h>
#include "core/pack/PackRegistry.h"

namespace rigkit {

rigSvg::rigSvg() : IPack("rigSvg") {
	

}

bool rigSvg::init() {
	spdlog::info("[rigSvg] init");
	return true;
}

void rigSvg::setup() {
	spdlog::info("[rigSvg] setup (load/write API ready)");
}

} // namespace rigkit

namespace {
struct rigSvgRegistrar {
	rigSvgRegistrar() {
		rigkit::PackRegistry::instance().addFactory("rigSvg", []() {
			return std::shared_ptr<rigkit::IPack>(std::make_shared<rigkit::rigSvg>());
		});
	}
};
static rigSvgRegistrar rigSvg_auto_reg;
} // namespace
