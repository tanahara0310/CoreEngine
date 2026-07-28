#include "pch.h"
#include "WaterSettingsSection.h"

#ifdef USE_IMGUI

#include "WaterSurfaceParameterPanel.h"
#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "WaterEditorFacade.h"

void WaterSettingsSection::Initialize(WaterSurfaceParameterPanel* panel,
	CoreEngine::WaterRenderFeature* runtimeController, WaterEditorFacade* facade) {
	panel_ = panel;
	runtimeController_ = runtimeController;
	facade_ = facade;
}

void WaterSettingsSection::Serialize(nlohmann::json& out) const {
	if (panel_ && facade_) {
		panel_->SerializeSettings(out, *facade_);
	}
}

void WaterSettingsSection::Deserialize(const nlohmann::json& in) {
	if (panel_ && runtimeController_ && facade_) {
		panel_->DeserializeSettings(in, *runtimeController_, *facade_);
	}
}

#endif // USE_IMGUI
