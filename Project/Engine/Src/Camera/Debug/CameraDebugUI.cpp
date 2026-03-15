#include "CameraDebugUI.h"
#include "CameraActivationEditorModule.h"
#include "CameraKeyframeEditorModule.h"
#include "CameraListEditorModule.h"
#include "CameraParametersEditorModule.h"
#include "CameraSnapshotEditorModule.h"
#include "CameraTransformEditorModule.h"

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
            RegisterModule(std::make_unique<CameraParametersEditorModule>());
            RegisterModule(std::make_unique<CameraActivationEditorModule>());
            RegisterModule(std::make_unique<CameraSnapshotEditorModule>());
            RegisterModule(std::make_unique<CameraKeyframeEditorModule>());
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
            ImGui::Separator();

            // タブバーで整理
            if (ImGui::BeginTabBar("CameraTabs")) {
                // 登録済みモジュールをタブとして順次描画する。
                for (const auto& module : modules_) {
                    if (!module) {
                        continue;
                    }

                    if (ImGui::BeginTabItem(module->GetTabName())) {
                        module->Draw(context);
                        ImGui::EndTabItem();
                    }
                }

                ImGui::EndTabBar();
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
        return context;
    }

}

#endif // _DEBUG

