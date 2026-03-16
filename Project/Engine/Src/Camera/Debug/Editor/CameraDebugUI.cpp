#include "CameraDebugUI.h"
#include "Module/CameraKeyframeEditorModule.h"
#include "Module/CameraClipPlayerModule.h"
#include "Module/CameraFollowEditorModule.h"
#include "Module/CameraListEditorModule.h"
#include "Module/CameraParametersEditorModule.h"
#include "Module/CameraTransformEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>
#include "Camera/CameraManager.h"

namespace CoreEngine {

    CameraDebugUI::CameraDebugUI()
        : cameraManager_(nullptr)
    {
        // ここでは基盤のみを初期化し、具体的な機能は外部モジュール登録に委ねる。
    }

    void CameraDebugUI::Initialize(CameraManager* cameraManager)
    {
        cameraManager_ = cameraManager;

        // 基盤構築後の最初の機能として、カメラ一覧モジュールを1つだけ登録する。
        // 以後は同様に機能モジュールを1つずつ追加していく。
        if (modules_.empty()) {
            RegisterModule(std::make_unique<CameraListEditorModule>());
            RegisterModule(std::make_unique<CameraTransformEditorModule>());
            RegisterModule(std::make_unique<CameraFollowEditorModule>());
            RegisterModule(std::make_unique<CameraParametersEditorModule>());
            RegisterModule(std::make_unique<CameraKeyframeEditorModule>());
            RegisterModule(std::make_unique<CameraClipPlayerModule>());
        }
    }

    void CameraDebugUI::RegisterModule(std::unique_ptr<ICameraEditorModule> module)
    {
        if (!module) {
            return;
        }
        modules_.push_back(std::move(module));
    }

    void CameraDebugUI::ClearModules()
    {
        modules_.clear();
    }

    void CameraDebugUI::Draw()
    {
        if (!cameraManager_) {
            return;
        }

        CameraEditorContext context = BuildContext();

        // タブ外でも必要な処理（再生更新など）を一括更新する。
        for (const auto& module : modules_) {
            if (module) {
                module->Update(context);
            }
        }

        // 専用のカメラウィンドウを作成
        if (ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_None)) {
            ImGui::Text("登録カメラ数: %zu", cameraManager_->GetCameraCount());
            ImGui::Text("アクティブ3D: %s", cameraManager_->GetActiveCameraName(CameraType::Camera3D).c_str());
            ImGui::Separator();

            // タブ遷移ではなく縦長レイアウトで表示し、機能全体の見通しを維持する。
            bool expandAll = false;
            bool collapseAll = false;
            if (ImGui::Button("すべて展開")) {
                expandAll = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("すべて折りたたみ")) {
                collapseAll = true;
            }

            ImGui::Separator();

            // モジュール描画領域をスクロール可能にし、項目追加時の視認性を確保する。
            if (ImGui::BeginChild("CameraModuleVerticalLayout", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                for (size_t i = 0; i < modules_.size(); ++i) {
                    const auto& module = modules_[i];
                    if (!module) {
                        continue;
                    }

                    ImGui::PushID(static_cast<int>(i));

                    if (expandAll) {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    } else if (collapseAll) {
                        ImGui::SetNextItemOpen(false, ImGuiCond_Always);
                    }

                    if (ImGui::CollapsingHeader(module->GetTabName(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        module->Draw(context);
                    }

                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::EndChild();
            }

            // 基盤状態ではモジュールが未登録でもエディターが空白にならないように案内を表示する。
            if (modules_.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("カメラエディターモジュールが登録されていません。");
            }
        }
        ImGui::End();
    }

    CameraEditorContext CameraDebugUI::BuildContext()
    {
        CameraEditorContext context{};
        context.cameraManager = cameraManager_;
        context.gameObjectManager = gameObjectManager_;
        return context;
    }

}

#endif // _DEBUG

