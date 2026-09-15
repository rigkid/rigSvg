#pragma once

#include "core/pack/IPack.h"
namespace rigkit {

/**
 * @brief SVG import/export pack - ParsedPathDoc / CPaths + paints.
 * @details Depends on rigPlotComponent.
 */
class rigSvg : public IPack {
  public:
	rigSvg();
	bool init() override;
	void setup() override;
	void onUiAttached() override;

  private:
	void registerFileThumbnails();
};

} // namespace rigkit
