#include "pch.h"
#include "CameraDebugUI.h"
#include "Module/CameraKeyframeEditorModule.h"
#include "Module/CameraRigEditorModule.h"
#include "Module/CameraClipPlayerModule.h"
#include "Module/CameraFollowEditorModule.h"
#include "Module/CameraListEditorModule.h"
#include "Module/CameraParametersEditorModule.h"
#include "Module/CameraShakeEditorModule.h"
#include "Module/CameraTransformEditorModule.h"
#include "Module/CameraGameViewControlModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include "Camera/CameraManager.h"
#include "Camera/Rig/CameraRig.h"
#include "Camera/Sequence/CameraSequence.h"

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
            RegisterModule(std::make_unique<CameraRigEditorModule>());
            RegisterModule(std::make_unique<CameraKeyframeEditorModule>());
            RegisterModule(std::make_unique<CameraClipPlayerModule>());
            RegisterModule(std::make_unique<CameraShakeEditorModule>());
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

    void CameraDebugUI::DrawViewportOverlay(const Camera& viewCamera,
        const CameraEditorViewport& viewport)
    {
        if (!cameraManager_) {
            return;
        }

        CameraEditorContext context = BuildContext();
        for (const auto& module : modules_) {
            if (module) {
                module->DrawViewportOverlay(context, viewCamera, viewport);
            }
        }
    }

    void CameraDebugUI::DrawToolbar(const CameraEditorContext& context)
    {
        CameraManager* cameraManager = context.cameraManager;

        // ===== どちらの視点を覗くか =====
        // 描画・ギズモ・ピッキングが見るカメラなので、いちばん上に固定で置く。
        bool useScene = cameraManager->IsUsingSceneCamera();
        if (ImGui::RadioButton("エディタ視点 (1)", useScene)) {
            cameraManager->SetUseSceneCamera(true);
        }
        UI::SameLine();
        if (ImGui::RadioButton("ゲーム視点 (2)", !useScene)) {
            cameraManager->SetUseSceneCamera(false);
        }

        UI::SameLine();
        ImGui::TextDisabled("|");
        UI::SameLine();
        ImGui::Text("描画中");
        UI::SameLine();
        ImGui::TextColored(ImVec4(0.26f, 0.72f, 0.98f, 1.0f), "%s",
            cameraManager->GetViewCameraName().c_str());

        // ===== 今このカメラを動かしているのは誰か =====
        // 追従やコントローラが効かないとき、原因がここで分かるようにしておく。
        UI::SameLine();
        ImGui::TextDisabled("|");
        UI::SameLine();
        if (CameraSequence::IsActive()) {
            const std::string playingName = CameraSequence::GetPlayingName();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "駆動: シーケンス \"%s\"%s",
                playingName.empty() ? "(無名)" : playingName.c_str(),
                CameraSequence::IsPlaying() ? "" : " (一時停止中)");
            UI::SameLine();
            if (ImGui::SmallButton("停止")) {
                CameraSequence::Stop();
            }
        } else if (CameraRig::IsActive()) {
            // シーケンスはリグの上に重なるので、両方動いていればシーケンスが見える。
            // ここへ来るのはリグだけが握っているとき。
            const std::string rigName = CameraRig::GetActiveName();
            ImGui::TextColored(ImVec4(0.98f, 0.72f, 0.26f, 1.0f), "駆動: リグ \"%s\"",
                rigName.empty() ? "(無名)" : rigName.c_str());
            UI::SameLine();
            if (ImGui::SmallButton("停止")) {
                CameraRig::Deactivate();
            }
        } else {
            ImGui::TextDisabled("駆動: ゲーム / カメラコントローラ");
        }
    }

    void CameraDebugUI::DrawContent()
    {
        if (!cameraManager_) {
            return;
        }

        CameraEditorContext context = BuildContext();

        DrawToolbar(context);
        UI::Separator();

        if (modules_.empty()) {
            UI::Hint("カメラエディターモジュールが登録されていません。");
            return;
        }

        // 縦一列の折りたたみをやめてタブにする。前は 1 つ開くたびに下の内容が
        // 押し下がり、同じボタンの位置が毎回変わっていた。タブなら位置が動かない。
        if (ImGui::BeginTabBar("##CameraEditorTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (size_t i = 0; i < modules_.size(); ++i) {
                const auto& module = modules_[i];
                if (!module) {
                    continue;
                }

                if (ImGui::BeginTabItem(module->GetTabName())) {
                    ImGui::PushID(static_cast<int>(i));
                    module->Draw(context);
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    void CameraDebugUI::Draw()
    {
        // モジュールの更新は SceneDebugEditor::Update() → CameraManager::UpdateDebugModules()
        // に一本化する。ここで呼ぶと 1 フレームに 2 回更新されてしまう。
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
        context.engineSystem = engineSystem_;
        return context;
    }

}

#endif // _DEBUG

