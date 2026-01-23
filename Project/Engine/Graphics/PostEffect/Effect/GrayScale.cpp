#include "GrayScale.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"


namespace CoreEngine
{
void GrayScale::DrawImGui()
{
#ifdef _DEBUG
	ImGui::PushID("GrayScale");
	
	ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
	ImGui::Text("画像をグレースケールに変換します");
	ImGui::Separator();
	
	if (!IsEnabled()) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効です");
	}
	
	ImGui::PopID();
#endif // _DEBUG
}
}
