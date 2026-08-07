#include "pch.h"
#include "ObjectSelector.h"
#include "GameObject/GameObject.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "GameObject/GameObjectManager.h"
#include "Editor/ImGui/GameObjectDebugAccess.h"
#include "Camera/Camera.h"
#include "WinApp/WinApp.h"
#include "Math/Matrix/Matrix4x4.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/ModelData.h"
#include "Graphics/Render/RenderPassType.h"
#include "Math/MathCore.h"
#include "Math/Geometry/Shapes.h"
#include "Math/Geometry/RayCast.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include "Editor/ImGui/ImGuiAll.h"

namespace CoreEngine
{
    void ObjectSelector::Initialize()
    {
        selectedObject_ = nullptr;
        selectedSprite_ = nullptr;
        gizmoMode_ = Gizmo::Mode::Translate;
    }

    void ObjectSelector::Update(GameObjectManager* gameObjectManager, const Camera* camera,
        const Vector2& mousePos, bool isViewportHovered)
    {
        if (!gameObjectManager || !camera) {
            return;
        }

        // ギズモを操作中は選択処理をスキップ
        if (Gizmo::IsUsing()) {
            return;
        }

        // ビューポートがホバー状態で、マウスの左ボタンがクリックされた場合
        if (isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // ギズモ上でクリックした場合は選択処理をスキップ
            if (!Gizmo::IsOver()) {
                GameObject* hitObject = RaycastObject(gameObjectManager, camera, mousePos);
                if (hitObject) {
                    SelectObject(hitObject);
                } else {
                    ClearSelection();
                }
            }
        }

        // キーボードでギズモモードを切り替え（W:移動 / E:回転 / R:拡縮）
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            SetGizmoMode(Gizmo::Mode::Translate);
        }
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            SetGizmoMode(Gizmo::Mode::Rotate);
        }
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            SetGizmoMode(Gizmo::Mode::Scale);
        }
    }

    void ObjectSelector::DrawGizmo(const Camera* camera)
    {
        if (selectedObject_ && camera) {
            auto* modelObj = DebugAccess::AsModelObject(selectedObject_);
            if (!Gizmo::IsUsing() && modelObj) {
                beforeGizmoTranslate_ = modelObj->GetTransform().translate;
                beforeGizmoRotate_ = modelObj->GetTransform().rotate;
                beforeGizmoScale_ = modelObj->GetTransform().scale;
                beforeGizmoActive_ = selectedObject_->IsActive();
            }

            Gizmo::Manipulate(selectedObject_, camera, gizmoMode_);

            // ギズモ操作中→操作完了の遷移を検出
            bool isUsing = Gizmo::IsUsing();
            if (wasGizmoUsing_ && !isUsing) {
                if (onTransformChanged_) {
                    onTransformChanged_(selectedObject_);
                }
                if (onGizmoEditCommitted_) {
                    onGizmoEditCommitted_(selectedObject_,
                        beforeGizmoTranslate_, beforeGizmoRotate_, beforeGizmoScale_, beforeGizmoActive_);
                }
            }
            wasGizmoUsing_ = isUsing;
        }
    }

    void ObjectSelector::Update2D(GameObjectManager* gameObjectManager, const Camera* camera,
        const Vector2& mousePos, bool isViewportHovered)
    {
        if (!gameObjectManager || !camera) {
            return;
        }

        // ギズモを操作中は選択処理をスキップ
        if (Gizmo::IsUsing()) {
            return;
        }

        // ビューポートがホバー状態で、マウスの左ボタンがクリックされた場合
        if (isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // ギズモ上でクリックした場合は選択処理をスキップ
            if (!Gizmo::IsOver()) {
                SpriteObject* hitSprite = RaycastSprite(gameObjectManager, camera, mousePos);
                if (hitSprite) {
                    SelectSprite(hitSprite);
                } else {
                    // スプライト選択のみクリア（3Dオブジェクトの選択はUpdate()側で管理する）
                    selectedSprite_ = nullptr;
                }
            }
        }

        // キーボードでギズモモードを切り替え（W:移動 / E:回転 / R:拡縮）
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            SetGizmoMode(Gizmo::Mode::Translate);
        }
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            SetGizmoMode(Gizmo::Mode::Rotate);
        }
        if (isViewportHovered && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            SetGizmoMode(Gizmo::Mode::Scale);
        }
    }

    void ObjectSelector::DrawGizmo2D(const Camera* camera)
    {
        if (selectedSprite_ && camera) {
            // ギズモ非使用中は操作前スナップショットを連続更新する
            if (!Gizmo::IsUsing()) {
                beforeGizmoTranslate_ = selectedSprite_->GetSpriteTransform().translate;
                beforeGizmoRotate_ = selectedSprite_->GetSpriteTransform().rotate;
                beforeGizmoScale_ = selectedSprite_->GetSpriteTransform().scale;
                beforeGizmoActive_ = selectedSprite_->IsActive();
            }

            Gizmo::Manipulate2D(selectedSprite_, camera, gizmoMode_);

            // ギズモ操作中→操作完了の遷移を検出
            bool isUsing = Gizmo::IsUsing();
            if (wasGizmoUsing_ && !isUsing) {
                if (onTransformChanged_) {
                    onTransformChanged_(selectedSprite_);
                }
                if (onGizmoEditCommitted_) {
                    onGizmoEditCommitted_(selectedSprite_,
                        beforeGizmoTranslate_, beforeGizmoRotate_, beforeGizmoScale_, beforeGizmoActive_);
                }
            }
            wasGizmoUsing_ = isUsing;
        }
    }

    Vector2 ObjectSelector::ScreenToWorld2D(const Vector2& mousePos, const Camera* camera)
    {
        // 2D（正射影）カメラでなければ変換できない
        if (!camera || camera->GetCameraType() != CameraType::Camera2D) {
            return Vector2(0.0f, 0.0f);
        }

        // スクリーンサイズ（2D カメラの正射影はウィンドウのクライアント領域に一致する）
        const Vector2 screenSize = {
            static_cast<float>(WinApp::GetCurrentClientWidthStatic()),
            static_cast<float>(WinApp::GetCurrentClientHeightStatic())
        };

        // 正規化座標（0.0〜1.0）をスクリーン座標に変換
        // 画面中央が原点、Y軸上が正
        const float screenX = (mousePos.x - 0.5f) * screenSize.x;
        const float screenY = (0.5f - mousePos.y) * screenSize.y;  // Y軸反転

        // カメラの位置とズームを考慮してワールド座標に変換
        const Vector3 cameraPos = camera->GetTranslate();
        const float zoom = camera->GetZoom();
        if (zoom == 0.0f) {
            return Vector2(0.0f, 0.0f);
        }

        return Vector2(screenX / zoom + cameraPos.x, screenY / zoom + cameraPos.y);
    }

    SpriteObject* ObjectSelector::RaycastSprite(GameObjectManager* gameObjectManager,
        const Camera* camera, const Vector2& mousePos)
    {
        // マウス位置をワールド座標に変換
        Vector2 worldMousePos = ScreenToWorld2D(mousePos, camera);

        const auto& objects = gameObjectManager->GetAllObjects();
        SpriteObject* closestSprite = nullptr;
        int highestOrder = INT_MIN;

        // スプライトオブジェクトのみをチェック
        for (const auto& obj : objects) {
            if (!obj->IsActive()) {
                continue;
            }

            // スプライトオブジェクトかどうかをチェック
            if (obj->GetRenderPassType() != RenderPassType::Sprite) {
                continue;
            }

            SpriteObject* sprite = dynamic_cast<SpriteObject*>(obj.get());
            if (!sprite) {
                continue;
            }

            // スプライトの矩形との当たり判定
            const EulerTransform& transform = sprite->GetSpriteTransform();
            Vector2 textureSize = sprite->GetTextureSize();
            Vector2 anchor = sprite->GetAnchor();

            // スプライトの実際のサイズを計算
            float actualWidth = textureSize.x * transform.scale.x;
            float actualHeight = textureSize.y * transform.scale.y;

            // アンカーポイントを考慮した矩形の範囲を計算
            float left = transform.translate.x - anchor.x * actualWidth;
            float right = transform.translate.x + (1.0f - anchor.x) * actualWidth;
            float bottom = transform.translate.y - anchor.y * actualHeight;
            float top = transform.translate.y + (1.0f - anchor.y) * actualHeight;

            // 矩形内にマウスがあるかチェック
            if (worldMousePos.x >= left && worldMousePos.x <= right &&
                worldMousePos.y >= bottom && worldMousePos.y <= top) {
                // 最前面（renderOrder が最大）のスプライトを優先選択
                int order = sprite->GetSortingLayer() * 1000 + sprite->GetOrderInLayer();
                if (order > highestOrder) {
                    highestOrder = order;
                    closestSprite = sprite;
                }
            }
        }

        return closestSprite;
    }

    void ObjectSelector::ScreenToWorldRay(const Vector2& mousePos, const Camera* camera,
        Vector3& rayOrigin, Vector3& rayDirection)
    {
        // カメラの位置を取得（レイの始点）
        rayOrigin = camera->GetPosition();

        // NDC座標に変換（0.0〜1.0 → -1.0〜1.0）
        float ndcX = mousePos.x * 2.0f - 1.0f;
        float ndcY = 1.0f - mousePos.y * 2.0f;  // Y軸を反転

        // プロジェクション行列とビュー行列を取得
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
        Matrix4x4 viewMatrix = camera->GetViewMatrix();

        // ニア平面上のNDC座標からビュー空間への変換
        // プロジェクション行列の逆変換を近似的に計算
        float fovY = 2.0f * std::atan(1.0f / projectionMatrix.m[1][1]);
        float aspectRatio = projectionMatrix.m[1][1] / projectionMatrix.m[0][0];

        // ビュー空間でのレイ方向を計算
        float viewX = ndcX * std::tan(fovY * 0.5f) * aspectRatio;
        float viewY = ndcY * std::tan(fovY * 0.5f);
        float viewZ = 1.0f;  // カメラの前方向

        // ビュー行列の逆変換（回転部分のみ転置）でワールド空間に変換
        // ビュー行列の回転部分（3x3）を転置してレイ方向に適用
        float worldX = viewX * viewMatrix.m[0][0] + viewY * viewMatrix.m[0][1] + viewZ * viewMatrix.m[0][2];
        float worldY = viewX * viewMatrix.m[1][0] + viewY * viewMatrix.m[1][1] + viewZ * viewMatrix.m[1][2];
        float worldZ = viewX * viewMatrix.m[2][0] + viewY * viewMatrix.m[2][1] + viewZ * viewMatrix.m[2][2];

        // 正規化
        float length = std::sqrt(worldX * worldX + worldY * worldY + worldZ * worldZ);
        if (length > 0.0f) {
            rayDirection = Vector3(
                worldX / length,
                worldY / length,
                worldZ / length
            );
        } else {
            rayDirection = Vector3(0.0f, 0.0f, 1.0f);
        }
    }

    bool ObjectSelector::RayIntersectsFallbackSphere(const Vector3& rayOrigin, const Vector3& rayDirection,
        GameObject* object, float radius, float& distance)
    {
        Geometry::RayHit hit{};
        if (!Geometry::Raycast(Geometry::Ray{ rayOrigin, rayDirection },
                               Geometry::Sphere{ object->GetWorldPosition(), radius }, &hit)) {
            return false;
        }
        distance = hit.distance;
        return true;
    }

    Vector3 ObjectSelector::TransformDirection(const Vector3& direction, const Matrix4x4& matrix)
    {
        // 平行移動を除いた 3x3 部分のみを適用する（方向ベクトル用・非正規化のまま返す）
        return Vector3(
            direction.x * matrix.m[0][0] + direction.y * matrix.m[1][0] + direction.z * matrix.m[2][0],
            direction.x * matrix.m[0][1] + direction.y * matrix.m[1][1] + direction.z * matrix.m[2][1],
            direction.x * matrix.m[0][2] + direction.y * matrix.m[1][2] + direction.z * matrix.m[2][2]
        );
    }

    bool ObjectSelector::RayIntersectsMesh(const Vector3& rayOrigin, const Vector3& rayDirection,
        GameObject* object, float& distance)
    {
        auto* modelObj = DebugAccess::AsModelObject(object);

        // メッシュを持たない場合の代替半径（スケールの最大成分。最低 1.0）
        auto fallbackRadius = [](const ModelGameObject* obj) {
            if (!obj) return 1.0f;
            const WorldTransform& t = obj->GetTransform();
            return (std::max)({ t.scale.x, t.scale.y, t.scale.z, 1.0f });
            };

        Model* model = modelObj ? modelObj->GetModel() : nullptr;
        if (!model || !model->IsInitialized()) {
            return RayIntersectsFallbackSphere(rayOrigin, rayDirection, object, fallbackRadius(modelObj), distance);
        }

        const ModelResource* modelResource = model->GetModelResource();
        if (!modelResource || !modelResource->IsLoaded()) {
            return RayIntersectsFallbackSphere(rayOrigin, rayDirection, object, fallbackRadius(modelObj), distance);
        }

        const ModelData& modelData = modelResource->GetModelData();
        const std::vector<VertexData>& vertices = modelData.vertices;
        const std::vector<int32_t>& indices = modelData.indices;

        if (vertices.empty() || indices.empty()) {
            return RayIntersectsFallbackSphere(rayOrigin, rayDirection, object, fallbackRadius(modelObj), distance);
        }

        const WorldTransform& transform = modelObj->GetTransform();
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();

        // 頂点を毎回ワールド変換する代わりに、レイをローカル空間へ 1 回だけ変換する。
        // ローカルの t はスケールで歪むため、距離比較はワールド空間へ戻してから行う。
        Matrix4x4 invWorldMatrix = MathCore::Matrix::Inverse(worldMatrix);
        const Vector3 localOrigin = TransformPoint(rayOrigin, invWorldMatrix);
        const Vector3 localDirection = TransformDirection(rayDirection, invWorldMatrix);

        const float localDirLengthSq =
            localDirection.x * localDirection.x +
            localDirection.y * localDirection.y +
            localDirection.z * localDirection.z;
        if (!(localDirLengthSq > 0.0f) || !std::isfinite(localDirLengthSq)) {
            // 特異行列（スケール0等）は逆変換できないのでスフィア判定へフォールバック
            return RayIntersectsFallbackSphere(rayOrigin, rayDirection, object, fallbackRadius(modelObj), distance);
        }

        // ローカル空間のレイ（方向は非正規化のまま。t の大小関係は保たれる）
        const Geometry::Ray localRay{ localOrigin, localDirection };

        // ローカルAABBで事前棄却（大半のオブジェクトはここで終わる）
        const BoundingBox& localAABB = modelResource->GetLocalBoundingBox();
        if (localAABB.IsValid() && !Geometry::Raycast(localRay, localAABB)) {
            return false;
        }

        float closestT = (std::numeric_limits<float>::max)();
        bool hit = false;

        // 全ての三角形をチェック（ローカル空間のまま判定するので頂点変換は不要）
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            // インデックスから頂点を取得
            int32_t idx0 = indices[i];
            int32_t idx1 = indices[i + 1];
            int32_t idx2 = indices[i + 2];

            // インデックスが範囲内かチェック
            if (idx0 < 0 || idx0 >= static_cast<int32_t>(vertices.size()) ||
                idx1 < 0 || idx1 >= static_cast<int32_t>(vertices.size()) ||
                idx2 < 0 || idx2 >= static_cast<int32_t>(vertices.size())) {
                continue;
            }

            const Vector3 v0Local = Vector3(
                vertices[idx0].position.x,
                vertices[idx0].position.y,
                vertices[idx0].position.z
            );
            const Vector3 v1Local = Vector3(
                vertices[idx1].position.x,
                vertices[idx1].position.y,
                vertices[idx1].position.z
            );
            const Vector3 v2Local = Vector3(
                vertices[idx2].position.x,
                vertices[idx2].position.y,
                vertices[idx2].position.z
            );

            // レイと三角形の交差判定（方向は非正規化でも t の大小関係は保たれる）
            Geometry::RayHit triangleHit{};
            if (Geometry::RaycastTriangle(localRay, v0Local, v1Local, v2Local, &triangleHit,
                                          0.0f, closestT)) {
                closestT = triangleHit.distance;
                hit = true;
            }
        }

        if (hit) {
            // ローカル空間の交点をワールドへ戻し、オブジェクト間で比較可能な距離にする
            const Vector3 localHit = Vector3(
                localOrigin.x + localDirection.x * closestT,
                localOrigin.y + localDirection.y * closestT,
                localOrigin.z + localDirection.z * closestT
            );
            const Vector3 worldHit = TransformPoint(localHit, worldMatrix);
            const Vector3 toHit = Vector3(
                worldHit.x - rayOrigin.x,
                worldHit.y - rayOrigin.y,
                worldHit.z - rayOrigin.z
            );
            distance = std::sqrt(toHit.x * toHit.x + toHit.y * toHit.y + toHit.z * toHit.z);
            return true;
        }

        return false;
    }

    Vector3 ObjectSelector::TransformPoint(const Vector3& point, const Matrix4x4& matrix)
    {
        // 同次座標に変換
        float x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
        float y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
        float z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
        float w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];

        // w除算
        if (w != 0.0f) {
            return Vector3(x / w, y / w, z / w);
        }
        return Vector3(x, y, z);
    }

    GameObject* ObjectSelector::RaycastObject(GameObjectManager* gameObjectManager,
        const Camera* camera, const Vector2& mousePos)
    {
        const auto& objects = gameObjectManager->GetAllObjects();
        GameObject* closestObject = nullptr;
        float closestDistance = (std::numeric_limits<float>::max)();

        // スクリーン座標からワールド空間のレイを生成
        Vector3 rayOrigin, rayDirection;
        ScreenToWorldRay(mousePos, camera, rayOrigin, rayDirection);

        for (const auto& obj : objects) {
            if (!obj->IsActive()) {
                continue;
            }

            // メッシュとの交差判定を試行
            float distance;
            if (RayIntersectsMesh(rayOrigin, rayDirection, obj.get(), distance)) {
                // より近いオブジェクトを選択
                if (distance < closestDistance) {
                    closestDistance = distance;
                    closestObject = obj.get();
                }
            }
        }

        return closestObject;
    }
}
