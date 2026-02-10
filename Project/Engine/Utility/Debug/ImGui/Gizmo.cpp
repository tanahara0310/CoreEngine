#include "Gizmo.h"
#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/WorldTransfom/WorldTransform.h"
#include <numbers>

namespace CoreEngine
{
    ImVec2 Gizmo::viewportPos_ = ImVec2(0, 0);
    ImVec2 Gizmo::viewportSize_ = ImVec2(0, 0);

    // 度数からラジアンへの変換
    constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0f;
    // ラジアンから度数への変換
    constexpr float kRadToDeg = 180.0f / static_cast<float>(std::numbers::pi);

    void Gizmo::Prepare(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        viewportPos_ = viewportPos;
        viewportSize_ = viewportSize;

        // ImGuizmoのビューポート設定
        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
        
        // ImGuizmoを有効化
        ImGuizmo::Enable(true);
    }

    bool Gizmo::Manipulate(GameObject* object, const ICamera* camera, Mode mode)
    {
        if (!object || !camera) {
            return false;
        }

        // ギズモの操作タイプを設定
        ImGuizmo::OPERATION operation;
        switch (mode) {
        case Mode::Translate:
            operation = ImGuizmo::TRANSLATE;
            break;
        case Mode::Rotate:
            operation = ImGuizmo::ROTATE;
            break;
        case Mode::Scale:
            operation = ImGuizmo::SCALE;
            break;
        default:
            operation = ImGuizmo::TRANSLATE;
            break;
        }

        // カメラのビュー行列とプロジェクション行列を取得
        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

        // オブジェクトのワールド行列を取得
        WorldTransform& transform = object->GetTransform();
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();

        // ImGuizmoで操作
        ImGuizmo::SetOrthographic(false);
        
        bool changed = ImGuizmo::Manipulate(
            &viewMatrix.m[0][0],
            &projectionMatrix.m[0][0],
            operation,
            ImGuizmo::LOCAL,
            &worldMatrix.m[0][0]
        );

        // トランスフォームが変更された場合、オブジェクトに反映
        if (changed) {
            // ワールド行列から位置、回転、スケールを抽出
            // 注意: DecomposeMatrixToComponentsは回転を度数法で返す
            Vector3 translation, rotationDegrees, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                &worldMatrix.m[0][0],
                &translation.x,
                &rotationDegrees.x,
                &scale.x
            );

            // オブジェクトのトランスフォームを更新
            transform.translate = translation;
            
            // 回転は度数からラジアンに変換
            transform.rotate = Vector3(
                rotationDegrees.x * kDegToRad,
                rotationDegrees.y * kDegToRad,
                rotationDegrees.z * kDegToRad
            );
            
            transform.scale = scale;
        }

        return changed;
    }

    bool Gizmo::Manipulate2D(SpriteObject* sprite, const ICamera* camera, Mode mode)
    {
        if (!sprite || !camera) {
            return false;
        }

        // ギズモの操作タイプを設定（2Dなので回転はZ軸のみ）
        ImGuizmo::OPERATION operation;
        switch (mode) {
        case Mode::Translate:
            operation = ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y;  // X,Y軸のみ
            break;
        case Mode::Rotate:
            operation = ImGuizmo::ROTATE_Z;  // Z軸回転のみ
            break;
        case Mode::Scale:
            operation = ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y;  // X,Yスケールのみ
            break;
        default:
            operation = ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y;
            break;
        }

        // カメラのビュー行列とプロジェクション行列を取得
        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

        // スプライトのトランスフォームを取得
        EulerTransform& spriteTransform = sprite->GetSpriteTransform();

        // 2D用ワールド行列を作成（Z座標は0で固定）
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(
            spriteTransform.scale,
            spriteTransform.rotate,
            spriteTransform.translate
        );

        // ImGuizmoで操作（Orthographicモード）
        ImGuizmo::SetOrthographic(true);
        
        bool changed = ImGuizmo::Manipulate(
            &viewMatrix.m[0][0],
            &projectionMatrix.m[0][0],
            operation,
            ImGuizmo::LOCAL,
            &worldMatrix.m[0][0]
        );

        // トランスフォームが変更された場合、スプライトに反映
        if (changed) {
            // ワールド行列から位置、回転、スケールを抽出
            Vector3 translation, rotationDegrees, scale;
            ImGuizmo::DecomposeMatrixToComponents(
                &worldMatrix.m[0][0],
                &translation.x,
                &rotationDegrees.x,
                &scale.x
            );

            // スプライトのトランスフォームを更新（Z座標は0に固定）
            spriteTransform.translate = Vector3(translation.x, translation.y, 0.0f);
            
            // 回転は度数からラジアンに変換（Z軸回転のみ使用）
            spriteTransform.rotate = Vector3(0.0f, 0.0f, rotationDegrees.z * kDegToRad);
            
            // スケール（Z軸は1.0固定）
            spriteTransform.scale = Vector3(scale.x, scale.y, 1.0f);
        }

        return changed;
    }

    bool Gizmo::IsUsing()
    {
        return ImGuizmo::IsUsing();
    }

    bool Gizmo::IsOver()
    {
        return ImGuizmo::IsOver();
    }
}
