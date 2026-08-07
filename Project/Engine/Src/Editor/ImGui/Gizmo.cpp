#include "pch.h"
#include "Gizmo.h"
#include "GameObject/GameObject.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "GameObject/Component/ITransformSource.h"
#include "GameObject/Component/MeshRendererComponent.h"
#include "GameObject/Component/TransformComponent.h"
#include "Math/MathCore.h"
#include "Editor/ImGui/GameObjectDebugAccess.h"
#include "Camera/Camera.h"
#include "WorldTransform/WorldTransform.h"
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

    bool Gizmo::Manipulate(GameObject* object, const Camera* camera, Mode mode)
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

        // 具象クラス（ModelGameObject）ではなく `ITransformSource` で引く。
        // 実体が WorldTransform（3D モデル）でも EulerTransform（パーティクルエミッタ等）でも
        // 同じ経路で通るので、**以前ダウンキャストの分岐から漏れてギズモが効かなかった
        // ParticleSystem / GpuParticleSystem でも効く**。
        // トランスフォームを持たないオブジェクト（デバッグ線など）はギズモの対象外。
        auto* source = object->GetComponent<ITransformSource>();
        if (!source) return false;

        Matrix4x4 viewMatrix       = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

        // ワールド行列は、階層（親）を持ちうる TransformComponent があればそれを使う。
        // 無い場合（EulerTransformComponent）は SRT から組み立てる。
        Matrix4x4 worldMatrix;
        if (auto* transformComponent = object->GetComponent<TransformComponent>()) {
            worldMatrix = transformComponent->Get().GetWorldMatrix();
        } else {
            worldMatrix = MathCore::Matrix::MakeAffine(
                source->Scale(), source->Rotate(), source->Translate());
        }

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

            // オブジェクトのトランスフォームを更新（実体の型を問わず ITransformSource 経由）
            source->Translate() = translation;

            // 回転は度数からラジアンに変換
            source->Rotate() = Vector3(
                rotationDegrees.x * kDegToRad,
                rotationDegrees.y * kDegToRad,
                rotationDegrees.z * kDegToRad
            );

            source->Scale() = scale;
        }

        return changed;
    }

    bool Gizmo::Manipulate2D(SpriteObject* sprite, const Camera* camera, Mode mode)
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
