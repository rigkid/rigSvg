#include "rigSvg.h"

#include "SvgThumbnail.h"
#include "core/IMui.h"
#include "core/RigKitEngine.h"
#include "core/pack/PackRegistry.h"

#include <spdlog/spdlog.h>

namespace rigkit {

rigSvg::rigSvg() : IPack("rigSvg") {
	

}

bool rigSvg::init() {
	spdlog::info("[rigSvg] init");
	return true;
}

void rigSvg::setup() {
	spdlog::info("[rigSvg] setup (load/write API ready)");
	registerFileThumbnails();
}

void rigSvg::onUiAttached() {
	registerFileThumbnails();
}

void rigSvg::registerFileThumbnails() {
	auto* engine = getEngine();
	auto* ui = engine ? engine->getUiManager() : nullptr;
	if (!ui) {
		return;
	}
	ui->registerFileThumbnailDecoder(".svg", [](const std::string& path, int maxEdge,
											   IMui::FileThumbnailPixels& out) {
		return svg::rasterizeFileThumbnail(path, maxEdge, out.w, out.h, out.rgba);
	});
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
