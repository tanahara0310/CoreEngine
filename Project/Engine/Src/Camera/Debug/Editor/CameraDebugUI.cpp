#include "CameraDebugUI.h"
#include "Module/CameraKeyframeEditorModule.h"
#include "Module/CameraClipPlayerModule.h"
#include "Module/CameraFollowEditorModule.h"
#include "Module/CameraListEditorModule.h"
#include "Module/CameraParametersEditorModule.h"
#include "Module/CameraTransformEditorModule.h"
#include "Module/CameraGameViewControlModule.h"

#ifdef USE_IMGUI

#include "Utility/Debug/ImGui/ImGuiAll.h"
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
            RegisterModule(std::make_unique<CameraGameViewControlModule>());
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

    void CameraDebugUI::UpdateModules()
    {
        if (!cameraManager_) {
            return;
        }
        CameraEditorContext context = BuildContext();
        for (const auto& module : modules_) {
            if (module) {
                module->Update(context);
            }
        }
    }

    void CameraDebugUI::DrawContent()
    {
        if (!cameraManager_) {
            return;
        }

        CameraEditorContext context = BuildContext();

        ImGui::Text("登録カメラ数: %zu", cameraManager_->GetCameraCount());
        ImGui::Text("アクティブ3D: %s", cameraManager_->GetActiveCameraName(CameraType::Camera3D).c_str());
        UI::Separator();

        bool expandAll = false;
        bool collapseAll = false;
        if (ImGui::Button("すべて展開")) {
            expandAll = true;
        }
        UI::SameLine();
        if (ImGui::Button("すべて折りたたみ")) {
            collapseAll = true;
        }

        UI::Separator();

        if (auto child = UI::Scope::ChildScope("CameraModuleVerticalLayout",
            ImVec2(0.0f, 0.0f), 0, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
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

                if (auto s = UI::Scope::TreeScope(module->GetTabName(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    module->Draw(context);
                }

                UI::Separator();
                ImGui::PopID();
            }
        }

        if (modules_.empty()) {
            UI::Separator();
            UI::Hint("カメラエディターモジュールが登録されていません。");
        }
    }

    void CameraDebugUI::Draw()
    {
        UpdateModules();

        if (ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_None)) {
            DrawContent();
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

