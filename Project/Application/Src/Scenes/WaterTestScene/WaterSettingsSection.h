#pragma once

#ifdef USE_IMGUI

#include "EngineSystem/Settings/IEditorSettingsSection.h"

class WaterSurfaceParameterPanel;
namespace CoreEngine { class WaterRenderFeature; }
class WaterEditorFacade;

/// @brief 水面エディタ設定の自動保存セクション
/// @details シリアライズ実装は WaterSurfaceParameterPanel（UI キャッシュの所有者）へ委譲する。
///          シーン（WaterSceneController）寿命。登録は WaterSceneController::Initialize、
///          解除は同デストラクタで行う
class WaterSettingsSection : public CoreEngine::IEditorSettingsSection {
public:
	/// @brief 委譲先を設定する（いずれも本セクションより長く生存させること）
	void Initialize(WaterSurfaceParameterPanel* panel,
		CoreEngine::WaterRenderFeature* runtimeController, WaterEditorFacade* facade);

	const char* GetSectionName() const override { return "Water"; }
	void Serialize(nlohmann::json& out) const override;
	void Deserialize(const nlohmann::json& in) override;

private:
	WaterSurfaceParameterPanel* panel_ = nullptr;
	CoreEngine::WaterRenderFeature* runtimeController_ = nullptr;
	WaterEditorFacade* facade_ = nullptr;
};

#endif // USE_IMGUI
