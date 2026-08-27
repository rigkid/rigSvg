#pragma once
#include <glm/glm.hpp>

#include "core/U_core.h"
#include "ecs/MEcs.h"

/**
 * @brief Load an SVG and present it as plain mesh entities.
 * @details rigSvg parses the file, pathEdit::makeDocMeshes bakes it into
 * CMesh entities, and the normal rigSystems present path draws them. The app
 * itself only writes data: the root CTransform places and breathes the tiger.
 */
class SvgApp : public rigkit::IApp {
  public:
	SvgApp() {
		window().width = 960;
		window().height = 720;
		window().title = "rigSvg - tiger";
		window().samples = 4;
	}
	void parseCommandLineArgs(const rigkit::CommandLineArgs& args) override;
	void setup() override;
	void update(float dt) override;
	void draw() override {}

	bool smokeFailed() const { return m_smoke && !m_smokeOk; }

  private:
	entt::entity m_tiger = entt::null;
	glm::vec2 m_center{0.f};
	glm::vec2 m_basePos{0.f};
	float m_time = 0.f;
	float m_nextFpsLog = 5.f;
	bool m_smoke = false;
	bool m_smokeOk = false;
};
