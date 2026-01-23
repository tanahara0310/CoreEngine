#include "DockingUI.h"


namespace CoreEngine
{
void DockingUI::RegisterWindow(const std::string& windowName, DockArea area)
{
	registeredWindows_[windowName] = area;
	
	// レイアウトが既に初期化されている場合、動的にドッキング
	if (layoutInitialized_) {
		ImGuiID nodeId = GetNodeIdForArea(area);
		if (nodeId != 0) {
			ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
		}
	}
}

void DockingUI::UnregisterWindow(const std::string& windowName)
{
	registeredWindows_.erase(windowName);
}

void DockingUI::BeginDockSpaceHostWindow()
{
	// メインビューポートに合わせてホストウィンドウの位置・サイズを設定
	ImGuiViewport* vp = ImGui::GetMainViewport();

	// メニューバーの高さを取得
	float menuBarHeight = ImGui::GetFrameHeight();

	// メニューバーの下にドッキングエリアを配置
	ImVec2 pos = vp->Pos;
	pos.y += menuBarHeight; // メニューバーの分だけ下にずらす

	ImVec2 size = vp->Size;
	size.y -= menuBarHeight; // メニューバーの分だけ高さを減らす

	ImGui::SetNextWindowPos(pos);
	ImGui::SetNextWindowSize(size);
	ImGui::SetNextWindowViewport(vp->ID);

	// タイトルバーや移動不可など、ドッキング用の特殊フラグを設定
	ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus;

	// 見た目調整（角丸・枠線・余白）を0にして全面ホストウィンドウ化
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
	ImGui::PopStyleVar(3);

	// ドッキングスペースを作成（中央透過）
	ImGuiID dockId = ImGui::GetID("MyDockSpace");
	ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
}

void DockingUI::SetupDockSpace()
{
	// 初回のみドッキングレイアウトを構築
	if (layoutInitialized_)
		return;

	BuildDockLayout();
	layoutInitialized_ = true;
}

ImGuiID DockingUI::GetNodeIdForArea(DockArea area) const
{
	return nodeIds_[static_cast<int>(area)];
}

void DockingUI::BuildDockLayout()
{
	// ルートノード作成＆リセット
	ImGuiViewport* vp = ImGui::GetMainViewport();

	// メニューバーの高さを考慮したサイズを設定
	float menuBarHeight = ImGui::GetFrameHeight();
	ImVec2 dockSpaceSize = ImVec2(vp->Size.x, vp->Size.y - menuBarHeight);

	ImGuiID dockMain = ImGui::GetID("MyDockSpace");
	ImGui::DockBuilderRemoveNode(dockMain);
	ImGui::DockBuilderAddNode(dockMain, ImGuiDockNodeFlags_None);
	ImGui::DockBuilderSetNodeSize(dockMain, dockSpaceSize);

	// 1) 左 20% を分割
	ImGuiID idLeft, idCentre;
	ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, &idLeft, &idCentre);

	// 左側をさらに上下に分割
	ImGuiID idLeftTop, idLeftBottom;
	ImGui::DockBuilderSplitNode(idLeft, ImGuiDir_Down, 0.5f, &idLeftBottom, &idLeftTop);

	// 2) 右 25% を分割
	ImGuiID idCentre2, idRight;
	ImGui::DockBuilderSplitNode(idCentre, ImGuiDir_Right, 0.25f, &idRight, &idCentre2);

	// 3) 中央エリアを上下分割
	ImGuiID idTop, idBottom;
	ImGui::DockBuilderSplitNode(idCentre2, ImGuiDir_Down, 0.30f, &idBottom, &idTop);

	// 4) 下部を左右分割
	ImGuiID idLighting, idObjectControl;
	ImGui::DockBuilderSplitNode(idBottom, ImGuiDir_Left, 0.50f, &idLighting, &idObjectControl);

	// ノードIDを保存
	nodeIds_[static_cast<int>(DockArea::LeftTop)] = idLeftTop;
	nodeIds_[static_cast<int>(DockArea::LeftBottom)] = idLeftBottom;
	nodeIds_[static_cast<int>(DockArea::Center)] = idTop;
	nodeIds_[static_cast<int>(DockArea::Right)] = idRight;
	nodeIds_[static_cast<int>(DockArea::BottomLeft)] = idLighting;
	nodeIds_[static_cast<int>(DockArea::BottomRight)] = idObjectControl;

	// 登録されているウィンドウを各ノードにドッキング
	for (const auto& [windowName, area] : registeredWindows_) {
		ImGuiID nodeId = GetNodeIdForArea(area);
		if (nodeId != 0) {
			ImGui::DockBuilderDockWindow(windowName.c_str(), nodeId);
		}
	}

	// レイアウト構築完了
	ImGui::DockBuilderFinish(dockMain);
}
}
