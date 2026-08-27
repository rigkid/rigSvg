#include "app.h"

#include "core/pack/MPack.h"
#include "core/RigKitEngine.h"
#include "core/util/AppPaths.h"
#include "rendering/U_gladGlfw.h"
#include "packs/rigComponent/src/CMesh.h"
#include "packs/rigComponent/src/CTransform.h"
#include "packs/rigComponent/src/rigComponent.h"
#include "packs/rigPlotComponent/src/ParsedPathDoc.h"
#include "packs/rigPlotComponent/src/rigPlotComponent.h"
#include "packs/rigSvg/src/rigSvg.h"
#include "packs/rigSvg/src/SvgLoader.h"
#include "packs/rigSystems/src/rigSystems.h"
#include "packs/rigVectorEditor/src/DocMeshes.h"
#include "packs/rigVectorEditor/src/rigVectorEditor.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

void SvgApp::parseCommandLineArgs(const rigkit::CommandLineArgs& args) {
	IApp::parseCommandLineArgs(args);
	if (args.hasFlag("smoke")) {
		m_smoke = true;
	}
}

void SvgApp::setup() {
	spdlog::info("svg - rigSvg loadFile tiger");
	m_engine->setClearColor(0.11f, 0.12f, 0.15f, 1.f);
	auto* packs = m_engine->getPackManager();
	if (!packs) {
		return;
	}
	packs->registerPack(std::make_shared<rigkit::rigComponent>());
	packs->registerPack(std::make_shared<rigkit::rigSystems>());
	packs->registerPack(std::make_shared<rigkit::rigPlotComponent>());
	packs->registerPack(std::make_shared<rigkit::rigSvg>());
	packs->registerPack(std::make_shared<rigkit::rigVectorEditor>());
	packs->initAll();
	packs->setupAll();
	auto* ecs = m_engine->getECSManager();
	if (!ecs) {
		return;
	}

	const std::string svgPath = AppPaths::joinPath(AppPaths::getDataDir(), "tiger.svg");
	rigkit::plot::ParsedPathDoc doc;
	if (!rigkit::svg::loadFile(svgPath, doc) || doc.empty()) {
		spdlog::error("svg - loadFile failed '{}'", svgPath);
		return;
	}

	// Fit the document into the window, bake it once as mesh entities, and
	// place the root. From here on everything on screen is plain data that the
	// rigSystems present path draws.
	const float winW = static_cast<float>(window().width);
	const float winH = static_cast<float>(window().height);
	const float margin = 48.f;
	const float scale = std::min((winW - 2.f * margin) / std::max(doc.contentW, 1.f),
								 (winH - 2.f * margin) / std::max(doc.contentH, 1.f));
	m_tiger = rigkit::pathEdit::makeDocMeshes(*ecs, doc, scale, "tiger");

	m_center = {winW * 0.5f, winH * 0.5f};
	m_basePos = {m_center.x - (doc.minX + doc.contentW * 0.5f) * scale,
				 m_center.y - (doc.minY + doc.contentH * 0.5f) * scale};
	auto& xf = ecs->getComponent<rigkit::ecs::CTransform>(m_tiger);
	xf.position = {m_basePos.x, m_basePos.y, 0.f};

	size_t paths = 0;
	for (const auto& layer : doc.layers) {
		paths += layer.paths.paths.size();
	}
	const size_t meshes = ecs->view<rigkit::ecs::CMesh>().size();
	spdlog::info("svg - {} layers, {} paths -> {} mesh entities", doc.layers.size(), paths, meshes);

	if (m_smoke) {
		m_smokeOk = paths >= 100 && meshes >= 200;
		if (!m_smokeOk) {
			spdlog::error("svg --smoke failed paths={} meshes={}", paths, meshes);
		} else {
			spdlog::info("svg --smoke ok");
		}
		if (auto* win = m_engine->getWindow()) {
			glfwSetWindowShouldClose(win, GLFW_TRUE);
		}
	}
}

void SvgApp::update(float dt) {
	auto* ecs = m_engine ? m_engine->getECSManager() : nullptr;
	if (!ecs || m_tiger == entt::null) {
		return;
	}
	m_time += dt;

	// A gentle breathe around the window centre. Motion is one CTransform
	// write on the root — SHierarchy and the present path do the rest.
	const float zoom = 1.f + 0.05f * std::sin(m_time * 0.8f);
	auto& xf = ecs->getComponent<rigkit::ecs::CTransform>(m_tiger);
	xf.scale = {zoom, zoom, 1.f};
	xf.position = {m_center.x - (m_center.x - m_basePos.x) * zoom,
				   m_center.y - (m_center.y - m_basePos.y) * zoom, 0.f};

	if (m_time >= m_nextFpsLog) {
		m_nextFpsLog += 5.f;
		spdlog::info("svg - {} fps", m_engine->getCurrentFrameRate());
	}
}
