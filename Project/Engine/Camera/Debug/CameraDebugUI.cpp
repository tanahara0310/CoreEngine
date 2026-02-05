#include "CameraDebugUI.h"

#ifdef _DEBUG

#include <imgui.h>
#include <numbers>
#include <algorithm>
#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "Engine/Math/Easing/EasingUtil.h"
#include "Engine/Utility/JsonManager/JsonManager.h"

using namespace CoreEngine::MathCore;

namespace CoreEngine {

    CameraDebugUI::CameraDebugUI()
        : cameraManager_(nullptr)
        , showAdvancedSettings_(false)
        , releaseCameraLookAtMode_(false)
        , releaseCameraLookAtTarget_{ 0.0f, 0.0f, 0.0f }
        , animFromFileIndex_(0)
        , animToFileIndex_(0)
        , animation_{}
        , animDuration_(1.0f)
        , easingTypeIndex_(0)
    {
    }

    void CameraDebugUI::Initialize(CameraManager* cameraManager)
    {
        cameraManager_ = cameraManager;
    }

    void CameraDebugUI::Draw()
    {
        if (!cameraManager_) {
            return;
        }

        // アニメーション更新
        UpdateAnimation();

        // 専用のカメラウィンドウを作成
        if (ImGui::Begin("Camera", nullptr, ImGuiWindowFlags_None)) {
            ImGui::Text("登録カメラ数: %zu", cameraManager_->GetCameraCount());
            ImGui::Separator();

            // タブバーで整理
            if (ImGui::BeginTabBar("CameraTabs")) {

                // 3Dカメラタブ
                if (ImGui::BeginTabItem("3D Camera")) {
                    DrawCamera3DSection();
                    ImGui::EndTabItem();
                }

                // 2Dカメラタブ
                if (ImGui::BeginTabItem("2D Camera")) {
                    DrawCamera2DSection();
                    ImGui::EndTabItem();
                }

                // プリセットタブ
                if (ImGui::BeginTabItem("Presets")) {
                    DrawPresetSection();
                    ImGui::EndTabItem();
                }

                // アニメーションタブ
                if (ImGui::BeginTabItem("Animation")) {
                    DrawAnimationSection();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void CameraDebugUI::DrawCamera3DSection()
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "3Dカメラ");
        ImGui::Separator();

        // 3Dカメラ一覧と切り替え
        bool has3DCamera = false;
        const auto& allCameras = cameraManager_->GetAllCameras();
        std::string activeName = cameraManager_->GetActiveCameraName(CameraType::Camera3D);

        // カメラ切り替えラジオボタン
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() == CameraType::Camera3D) {
                has3DCamera = true;
                bool isActive = (name == activeName);
                if (ImGui::RadioButton(name.c_str(), isActive)) {
                    cameraManager_->SetActiveCamera(name, CameraType::Camera3D);
                }
            }
        }

        if (!has3DCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "3Dカメラが登録されていません");
            return;
        }

        ImGui::Separator();

        // アクティブカメラの制御
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) return;

        ImGui::Text("アクティブ: %s", activeName.c_str());

        // カメラ基本情報
        DrawCameraBasicInfo(activeCamera);

        ImGui::Separator();

        // カメラタイプ別のコントロール
        DebugCamera* debugCam = dynamic_cast<DebugCamera*>(activeCamera);
        if (debugCam) {
            DrawDebugCameraControls(debugCam);
        } else {
            Camera* releaseCam = dynamic_cast<Camera*>(activeCamera);
            if (releaseCam) {
                DrawReleaseCameraControls(releaseCam);
            }
        }
    }

    void CameraDebugUI::DrawCamera2DSection()
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "2Dカメラ");
        ImGui::Separator();

        // 2Dカメラ一覧と切り替え
        bool has2DCamera = false;
        const auto& allCameras = cameraManager_->GetAllCameras();
        std::string activeName = cameraManager_->GetActiveCameraName(CameraType::Camera2D);

        // カメラ切り替えラジオボタン
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() == CameraType::Camera2D) {
                has2DCamera = true;
                bool isActive = (name == activeName);
                if (ImGui::RadioButton(name.c_str(), isActive)) {
                    cameraManager_->SetActiveCamera(name, CameraType::Camera2D);
                }
            }
        }

        if (!has2DCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "2Dカメラが登録されていません");
            return;
        }

        ImGui::Separator();

        // アクティブカメラの制御
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera2D);
        if (!activeCamera) return;

        ImGui::Text("アクティブ: %s", activeName.c_str());
        ImGui::Separator();

        // カメラ基本情報
        DrawCameraBasicInfo(activeCamera);
    }

    void CameraDebugUI::DrawCameraBasicInfo(ICamera* camera)
    {
        if (!camera) return;

        // 有効/無効切り替え
        bool isActive = camera->GetActive();
        if (ImGui::Checkbox("有効", &isActive)) {
            camera->SetActive(isActive);
        }

        // 位置情報
        Vector3 pos = camera->GetPosition();
        ImGui::Text("位置: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    }

    void CameraDebugUI::DrawDebugCameraControls(DebugCamera* debugCamera)
    {
        if (!debugCamera) return;

        ImGui::PushID("DebugCamera");

        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "デバッグカメラ制御");
        ImGui::Separator();

        // 操作状態表示
        if (debugCamera->IsControlling()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[操作中]");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "待機中");
        }

        ImGui::Separator();

        // ===== 注視点コントロール =====
        Vector3 target = debugCamera->GetTarget();
        if (ImGui::DragFloat3("注視点", &target.x, 0.1f)) {
            debugCamera->SetTarget(target);
        }

        // ===== 距離コントロール（対数スケール） =====
        float distance = debugCamera->GetDistance();
        if (ImGui::SliderFloat("距離", &distance, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            debugCamera->SetDistance(distance);
        }

        // ===== 角度コントロール（度数法で表示） =====
        float pitchDeg = debugCamera->GetPitch() * 180.0f / std::numbers::pi_v<float>;
        float yawDeg = debugCamera->GetYaw() * 180.0f / std::numbers::pi_v<float>;

        if (ImGui::SliderFloat("ピッチ", &pitchDeg, -89.0f, 89.0f, "%.1f°")) {
            debugCamera->SetPitch(pitchDeg * std::numbers::pi_v<float> / 180.0f);
        }

        if (ImGui::SliderFloat("ヨー", &yawDeg, 0.0f, 360.0f, "%.1f°")) {
            debugCamera->SetYaw(yawDeg * std::numbers::pi_v<float> / 180.0f);
        }

        ImGui::Separator();

        // ===== クイックビュープリセット =====
        ImGui::Text("クイックビュー:");

        if (ImGui::Button("正面")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Front);
        ImGui::SameLine();
        if (ImGui::Button("背面")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Back);
        ImGui::SameLine();
        if (ImGui::Button("左")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Left);
        ImGui::SameLine();
        if (ImGui::Button("右")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Right);

        if (ImGui::Button("上")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Top);
        ImGui::SameLine();
        if (ImGui::Button("下")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Bottom);
        ImGui::SameLine();
        if (ImGui::Button("斜め")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Diagonal);
        ImGui::SameLine();
        if (ImGui::Button("クローズアップ")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::CloseUp);

        ImGui::Separator();

        // ===== カメラ設定 =====
        if (ImGui::CollapsingHeader("カメラ設定", ImGuiTreeNodeFlags_None)) {
            auto settings = debugCamera->GetSettings();
            bool changed = false;

            changed |= ImGui::SliderFloat("回転感度", &settings.rotationSensitivity, 0.001f, 0.01f, "%.4f");
            changed |= ImGui::SliderFloat("パン感度", &settings.panSensitivity, 0.0001f, 0.001f, "%.4f");
            changed |= ImGui::SliderFloat("ズーム感度", &settings.zoomSensitivity, 0.1f, 5.0f, "%.2f");

            changed |= ImGui::Checkbox("スムーズ移動", &settings.smoothMovement);
            if (settings.smoothMovement) {
                ImGui::Indent();
                changed |= ImGui::SliderFloat("スムージング係数", &settings.smoothingFactor, 0.01f, 0.5f, "%.3f");
                ImGui::Unindent();
            }

            changed |= ImGui::Checkbox("Y軸反転", &settings.invertY);

            if (changed) {
                debugCamera->SetSettings(settings);
            }
        }

        ImGui::Separator();

        // ===== カメラパラメータセクション =====
        if (ImGui::CollapsingHeader("カメラパラメータ", ImGuiTreeNodeFlags_None)) {
            auto params = debugCamera->GetParameters();
            bool paramsChanged = false;

            // FOV（視野角）
            float fovDeg = params.GetFovDegrees();
            if (ImGui::SliderFloat("視野角 (FOV)", &fovDeg, 30.0f, 120.0f, "%.1f°")) {
                params.SetFovDegrees(fovDeg);
                paramsChanged = true;
            }

            // クリップ距離
            paramsChanged |= ImGui::DragFloat("Near Clip", &params.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
            paramsChanged |= ImGui::DragFloat("Far Clip", &params.farClip, 1.0f, 10.0f, 10000.0f, "%.1f");

            // アスペクト比（0 = 自動）
            paramsChanged |= ImGui::DragFloat("アスペクト比", &params.aspectRatio, 0.01f, 0.0f, 3.0f, "%.3f");
            ImGui::SameLine();

            // パラメータリセットボタン
            if (ImGui::Button("パラメータをリセット", ImVec2(-1, 0))) {
                debugCamera->ResetParameters();
            }

            if (paramsChanged) {
                debugCamera->SetParameters(params);
            }
        }

        ImGui::Separator();

        // ===== リセットボタン =====
        if (ImGui::Button("カメラをリセット", ImVec2(-1, 0))) {
            debugCamera->Reset();
        }

        ImGui::PopID();
    }

    void CameraDebugUI::DrawReleaseCameraControls(Camera* camera)
    {
        if (!camera) return;

        ImGui::PushID("ReleaseCamera");

        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "リリースカメラ制御");
        ImGui::Separator();

        // ===== LookAtモード切り替え =====
        ImGui::Checkbox("LookAtモード", &releaseCameraLookAtMode_);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("指定した位置を注視するモード");
        }

        if (releaseCameraLookAtMode_) {
            ImGui::Indent();
            if (ImGui::DragFloat3("注視点", &releaseCameraLookAtTarget_.x, 0.1f)) {
                camera->LookAt(releaseCameraLookAtTarget_);
            }
            ImGui::Unindent();
        }

        ImGui::Separator();

        // ===== SRT変換コントロール =====
        ImGui::Text("トランスフォーム:");

        Vector3 scale = camera->GetScale();
        Vector3 rotate = camera->GetRotate();
        Vector3 translate = camera->GetTranslate();

        bool changed = false;

        if (ImGui::DragFloat3("スケール", &scale.x, 0.01f, 0.01f, 10.0f)) {
            camera->SetScale(scale);
            changed = true;
        }

        // 回転を度数法で表示・編集
        Vector3 rotateDeg = {
            rotate.x * 180.0f / std::numbers::pi_v<float>,
            rotate.y * 180.0f / std::numbers::pi_v<float>,
            rotate.z * 180.0f / std::numbers::pi_v<float>
        };

        if (ImGui::DragFloat3("回転 (度)", &rotateDeg.x, 0.5f, -360.0f, 360.0f, "%.1f°")) {
            rotate = {
                rotateDeg.x * std::numbers::pi_v<float> / 180.0f,
                rotateDeg.y * std::numbers::pi_v<float> / 180.0f,
                rotateDeg.z * std::numbers::pi_v<float> / 180.0f
            };
            camera->SetRotate(rotate);
            changed = true;
        }

        if (ImGui::DragFloat3("位置", &translate.x, 0.1f)) {
            camera->SetTranslate(translate);
            changed = true;
        }

        ImGui::Separator();

        // ===== カメラ方向ベクトル情報 =====
        DrawCameraVectorInfo(camera);

        ImGui::Separator();

        // ===== カメラパラメータセクション =====
        if (ImGui::CollapsingHeader("カメラパラメータ", ImGuiTreeNodeFlags_None)) {
            auto params = camera->GetParameters();
            bool paramsChanged = false;

            // FOV（視野角）
            float fovDeg = params.GetFovDegrees();
            if (ImGui::SliderFloat("視野角 (FOV)", &fovDeg, 30.0f, 120.0f, "%.1f°")) {
                params.SetFovDegrees(fovDeg);
                paramsChanged = true;
            }

            // クリップ距離
            paramsChanged |= ImGui::DragFloat("Near Clip", &params.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
            paramsChanged |= ImGui::DragFloat("Far Clip", &params.farClip, 1.0f, 10.0f, 10000.0f, "%.1f");

            // アスペクト比（0 = 自動）
            paramsChanged |= ImGui::DragFloat("アスペクト比", &params.aspectRatio, 0.01f, 0.0f, 3.0f, "%.3f");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0.0で自動計算");
            }

            // パラメータリセットボタン
            if (ImGui::Button("パラメータをリセット", ImVec2(-1, 0))) {
                camera->ResetParameters();
            }

            if (paramsChanged) {
                camera->SetParameters(params);
            }
        }

        ImGui::Separator();

        // ラジアン表示（詳細情報）
        if (showAdvancedSettings_) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "詳細情報:");
            ImGui::Text("回転(rad): (%.3f, %.3f, %.3f)", rotate.x, rotate.y, rotate.z);
            ImGui::Separator();
        }

        // ===== 詳細設定トグル =====
        ImGui::Checkbox("詳細設定を表示", &showAdvancedSettings_);

        ImGui::Separator();

        // ===== カメラリセットボタン =====
        if (ImGui::Button("カメラをリセット", ImVec2(-1, 0))) {
            camera->Reset();
            releaseCameraLookAtMode_ = false;
            releaseCameraLookAtTarget_ = Vector3{ 0.0f, 0.0f, 0.0f };
        }

        ImGui::PopID();
    }

    void CameraDebugUI::DrawCameraVectorInfo(Camera* camera)
    {
        if (!camera) return;

        if (ImGui::CollapsingHeader("方向ベクトル情報", ImGuiTreeNodeFlags_None)) {
            Vector3 forward = camera->GetForward();
            Vector3 right = camera->GetRight();
            Vector3 up = camera->GetUp();

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "前方:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", forward.x, forward.y, forward.z);

            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "右:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", right.x, right.y, right.z);

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "上:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", up.x, up.y, up.z);
        }
    }

    // ===== プリセット管理セクション =====
    void CameraDebugUI::DrawPresetSection()
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "カメラプリセット (JSON)");
        ImGui::Separator();

        // アクティブカメラを取得
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "アクティブな3Dカメラがありません");
            return;
        }

        // プリセットマネージャーのUI表示
        presetManager_.ShowImGui(activeCamera);
    }

    // ===== アニメーションセクション =====
    void CameraDebugUI::DrawAnimationSection()
    {
        ImGui::TextColored(ImVec4(0.8f, 0.3f, 1.0f, 1.0f), "カメラアニメーション (JSON)");
        ImGui::Separator();

        // プリセットマネージャーからファイルリストを取得
        const auto& presetFiles = presetManager_.GetCurrentPresetList();

        if (presetFiles.size() < 2) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "アニメーションには2つ以上のプリセットが必要です");
            ImGui::Separator();
            ImGui::Text("Presetsタブでプリセットを保存してください。");
            return;
        }

        // アニメーション中の表示
        if (animation_.isAnimating) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[再生中]");
            ImGui::ProgressBar(animation_.progress, ImVec2(-1, 0));
            ImGui::Text("%.1f / %.1f秒", animation_.progress * animation_.duration, animation_.duration);

            if (ImGui::Button("停止", ImVec2(-1, 0))) {
                animation_.isAnimating = false;
            }
            return;
        }

        // アニメーション設定
        ImGui::Text("開始プリセット:");
        if (ImGui::BeginCombo("##FromFile", presetFiles[animFromFileIndex_].c_str())) {
            for (int i = 0; i < static_cast<int>(presetFiles.size()); i++) {
                bool isSelected = (i == animFromFileIndex_);
                if (ImGui::Selectable(presetFiles[i].c_str(), isSelected)) {
                    animFromFileIndex_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("終了プリセット:");
        if (ImGui::BeginCombo("##ToFile", presetFiles[animToFileIndex_].c_str())) {
            for (int i = 0; i < static_cast<int>(presetFiles.size()); i++) {
                bool isSelected = (i == animToFileIndex_);
                if (ImGui::Selectable(presetFiles[i].c_str(), isSelected)) {
                    animToFileIndex_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // アニメーション時間
        ImGui::DragFloat("時間 (秒)", &animDuration_, 0.1f, 0.1f, 10.0f, "%.1f");

        // イージングタイプ
        const char* easingTypes[] = { "Linear", "EaseInOut", "EaseIn", "EaseOut" };
        ImGui::Combo("イージング", &easingTypeIndex_, easingTypes, IM_ARRAYSIZE(easingTypes));

        ImGui::Separator();

        // 開始ボタン
        if (ImGui::Button("アニメーション開始", ImVec2(-1, 0))) {
            StartAnimation(animFromFileIndex_, animToFileIndex_);
        }
    }

    // アニメーション開始
    void CameraDebugUI::StartAnimation(int fromIndex, int toIndex)
    {
        // プリセットファイルリストを取得
        const auto& presetFiles = presetManager_.GetCurrentPresetList();

        if (fromIndex < 0 || fromIndex >= static_cast<int>(presetFiles.size())) return;
        if (toIndex < 0 || toIndex >= static_cast<int>(presetFiles.size())) return;

        // JSONファイルからスナップショットを読み込み
        ICamera* dummyCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!dummyCamera) return;

        // ディレクトリパスを取得（PresetManagerから）
        std::string directory = "Assets/Presets/Camera/";  // デフォルト

        std::string fromPath = directory + presetFiles[fromIndex];
        std::string toPath = directory + presetFiles[toIndex];

        // 一時的にJSONを読み込んでスナップショットに変換
        json fromJson = JsonManager::GetInstance().LoadJson(fromPath);
        json toJson = JsonManager::GetInstance().LoadJson(toPath);

        if (fromJson.empty() || toJson.empty()) {
            return;
        }

        // JSONからスナップショットに変換（PresetManagerのヘルパー関数は使えないのでここで実装）
        auto jsonToSnapshot = [](const json& jsonData) -> CameraSnapshot {
            CameraSnapshot snapshot;
            snapshot.isDebugCamera = JsonManager::SafeGet(jsonData, "isDebugCamera", false);

            if (snapshot.isDebugCamera) {
                snapshot.target = JsonManager::JsonToVector3(jsonData["target"]);
                snapshot.distance = JsonManager::SafeGet(jsonData, "distance", 20.0f);
                snapshot.pitch = JsonManager::SafeGet(jsonData, "pitch", 0.25f);
                snapshot.yaw = JsonManager::SafeGet(jsonData, "yaw", 3.14159265359f);
            } else {
                snapshot.position = JsonManager::JsonToVector3(jsonData["position"]);
                snapshot.rotation = JsonManager::JsonToVector3(jsonData["rotation"]);
                snapshot.scale = JsonManager::JsonToVector3(jsonData["scale"]);
            }

            if (jsonData.contains("parameters")) {
                const auto& params = jsonData["parameters"];
                snapshot.parameters.fov = JsonManager::SafeGet(params, "fov", 0.45f);
                snapshot.parameters.nearClip = JsonManager::SafeGet(params, "nearClip", 0.1f);
                snapshot.parameters.farClip = JsonManager::SafeGet(params, "farClip", 1000.0f);
                snapshot.parameters.aspectRatio = JsonManager::SafeGet(params, "aspectRatio", 0.0f);
            }

            return snapshot;
        };

        animation_.isAnimating = true;
        animation_.progress = 0.0f;
        animation_.duration = animDuration_;
        animation_.fromSnapshot = jsonToSnapshot(fromJson);
        animation_.toSnapshot = jsonToSnapshot(toJson);
    }

    // アニメーション更新
    void CameraDebugUI::UpdateAnimation()
    {
        if (!animation_.isAnimating) return;

        // デルタタイムを取得（仮で1/60秒として計算）
        float deltaTime = 1.0f / 60.0f;

        animation_.progress += deltaTime / animation_.duration;

        if (animation_.progress >= 1.0f) {
            animation_.progress = 1.0f;
            animation_.isAnimating = false;
        }

        // イージング関数を適用
        float t = animation_.progress;

        switch (easingTypeIndex_) {
        case 1: // EaseInOut
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseInOutQuad);
            break;
        case 2: // EaseIn
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseInQuad);
            break;
        case 3: // EaseOut
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseOutQuad);
            break;
        default: // Linear
            break;
        }

        // 補間されたスナップショットを作成
        CameraSnapshot interpolated;
        const auto& from = animation_.fromSnapshot;
        const auto& to = animation_.toSnapshot;

        interpolated.isDebugCamera = from.isDebugCamera;

        if (from.isDebugCamera) {
            // DebugCamera用の補間
            interpolated.target = EasingUtil::LerpVector3(from.target, to.target, t, EasingUtil::Type::Linear);
            interpolated.distance = from.distance + (to.distance - from.distance) * t;
            interpolated.pitch = from.pitch + (to.pitch - from.pitch) * t;
            interpolated.yaw = from.yaw + (to.yaw - from.yaw) * t;
        } else {
            // ReleaseCamera用の補間
            interpolated.position = EasingUtil::LerpVector3(from.position, to.position, t, EasingUtil::Type::Linear);
            interpolated.rotation = EasingUtil::LerpVector3(from.rotation, to.rotation, t, EasingUtil::Type::Linear);
            interpolated.scale = EasingUtil::LerpVector3(from.scale, to.scale, t, EasingUtil::Type::Linear);
        }

        // パラメータの補間
        interpolated.parameters.fov = from.parameters.fov + (to.parameters.fov - from.parameters.fov) * t;
        interpolated.parameters.nearClip = from.parameters.nearClip + (to.parameters.nearClip - from.parameters.nearClip) * t;
        interpolated.parameters.farClip = from.parameters.farClip + (to.parameters.farClip - from.parameters.farClip) * t;
        interpolated.parameters.aspectRatio = from.parameters.aspectRatio + (to.parameters.aspectRatio - from.parameters.aspectRatio) * t;

        // アクティブカメラに適用
        ICamera* activeCamera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) return;

        if (interpolated.isDebugCamera) {
            DebugCamera* debugCam = dynamic_cast<DebugCamera*>(activeCamera);
            if (debugCam) {
                debugCam->RestoreSnapshot(interpolated);
            }
        } else {
            Camera* releaseCam = dynamic_cast<Camera*>(activeCamera);
            if (releaseCam) {
                releaseCam->RestoreSnapshot(interpolated);
            }
        }
    }

}

#endif // _DEBUG

