#include "CameraParametersEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>

#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Camera/CameraStructs.h"

namespace CoreEngine
{
    void CameraParametersEditorModule::Update(const CameraEditorContext& context)
    {
        // 初期版では再生・補間のような時間更新を持たない。
        (void)context;
    }

    void CameraParametersEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        // まず3Dカメラを編集対象にし、将来必要であれば2D側にも拡張する。
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            ImGui::TextDisabled("アクティブな3Dカメラがありません。");
            return;
        }

        const std::string& activeName = context.cameraManager->GetActiveCameraName(CameraType::Camera3D);
        ImGui::Text("アクティブ3D: %s", activeName.c_str());
        ImGui::Separator();

        CameraParameters params = active3D->GetParameters();
        bool changed = false;

        // 視野角は度数法で編集し、内部はラジアンへ戻す。
        float fovDeg = params.GetFovDegrees();
        if (ImGui::SliderFloat("視野角 (度)", &fovDeg, 30.0f, 120.0f, "%.1f")) {
            params.SetFovDegrees(fovDeg);
            changed = true;
        }

        // クリップ距離は一般的な実用範囲を初期値として提供する。
        changed |= ImGui::DragFloat("ニアクリップ", &params.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
        changed |= ImGui::DragFloat("ファークリップ", &params.farClip, 1.0f, 10.0f, 100000.0f, "%.1f");

        // Aspect=0 は自動計算モードとして扱う。
        changed |= ImGui::DragFloat("アスペクト比 (0:自動)", &params.aspectRatio, 0.01f, 0.0f, 4.0f, "%.3f");

        // 不正値を防ぐため、最小限の整合性チェックを行ってから反映する。
        params.nearClip = (params.nearClip < 0.001f) ? 0.001f : params.nearClip;
        if (params.farClip <= params.nearClip) {
            params.farClip = params.nearClip + 0.001f;
        }

        if (changed) {
            active3D->SetParameters(params);
        }
    }
}

#endif // _DEBUG
