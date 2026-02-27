#include "ObjectSelector.h"
#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/ObjectCommon/SpriteObject.h"
#include "Engine/ObjectCommon/GameObjectManager.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Camera/Camera2D.h"
#include "Engine/Math/Matrix/Matrix4x4.h"
#include "Engine/WorldTransform/WorldTransform.h"
#include "Engine/Graphics/Model/Model.h"
#include "Engine/Graphics/Model/ModelResource.h"
#include "Engine/Graphics/Structs/ModelData.h"
#include "Engine/Graphics/Render/RenderPassType.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <imgui.h>

namespace CoreEngine
{
    void ObjectSelector::Initialize()
    {
        selectedObject_ = nullptr;
        selectedSprite_ = nullptr;
        gizmoMode_ = Gizmo::Mode::Translate;
    }

    void ObjectSelector::Update(GameObjectManager* gameObjectManager, const ICamera* camera,
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

        // キーボードでギズモモードを切り替え（Qキー：移動、Wキー：回転、Eキー：スケール）
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            SetGizmoMode(Gizmo::Mode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            SetGizmoMode(Gizmo::Mode::Rotate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            SetGizmoMode(Gizmo::Mode::Scale);
        }
    }

    void ObjectSelector::DrawGizmo(const ICamera* camera)
    {
        if (selectedObject_ && camera) {
            Gizmo::Manipulate(selectedObject_, camera, gizmoMode_);
        }
    }

    void ObjectSelector::Update2D(GameObjectManager* gameObjectManager, const ICamera* camera,
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

        // キーボードでギズモモードを切り替え（Qキー：移動、Wキー：回転、Eキー：スケール）
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            SetGizmoMode(Gizmo::Mode::Translate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            SetGizmoMode(Gizmo::Mode::Rotate);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            SetGizmoMode(Gizmo::Mode::Scale);
        }
    }

    void ObjectSelector::DrawGizmo2D(const ICamera* camera)
    {
        if (selectedSprite_ && camera) {
            Gizmo::Manipulate2D(selectedSprite_, camera, gizmoMode_);
        }
    }

    Vector2 ObjectSelector::ScreenToWorld2D(const Vector2& mousePos, const ICamera* camera)
    {
        // Camera2Dとしてキャスト
        const Camera2D* camera2D = dynamic_cast<const Camera2D*>(camera);
        if (!camera2D) {
            return Vector2(0.0f, 0.0f);
        }

        // スクリーンサイズを取得
        Vector2 screenSize = camera2D->GetScreenSize();
        
        // 正規化座標（0.0〜1.0）をスクリーン座標に変換
        // 画面中央が原点、Y軸上が正
        float screenX = (mousePos.x - 0.5f) * screenSize.x;
        float screenY = (0.5f - mousePos.y) * screenSize.y;  // Y軸反転

        // カメラの位置とズームを考慮してワールド座標に変換
        Vector2 cameraPos = camera2D->GetPosition2D();
        float zoom = camera2D->GetZoom();

        float worldX = screenX / zoom + cameraPos.x;
        float worldY = screenY / zoom + cameraPos.y;

        return Vector2(worldX, worldY);
    }

    SpriteObject* ObjectSelector::RaycastSprite(GameObjectManager* gameObjectManager,
                                                  const ICamera* camera, const Vector2& mousePos)
    {
        // マウス位置をワールド座標に変換
        Vector2 worldMousePos = ScreenToWorld2D(mousePos, camera);

        const auto& objects = gameObjectManager->GetAllObjects();
        SpriteObject* closestSprite = nullptr;

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
                closestSprite = sprite;
                // 最初に見つかったスプライトを選択（レイヤー順序を考慮する場合は要改善）
                break;
            }
        }

        return closestSprite;
    }

    void ObjectSelector::ScreenToWorldRay(const Vector2& mousePos, const ICamera* camera,
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

    bool ObjectSelector::RayIntersectsSphere(const Vector3& rayOrigin, const Vector3& rayDirection,
                                              const Vector3& sphereCenter, float sphereRadius, float& distance)
    {
        // レイの始点からスフィアの中心へのベクトル
        Vector3 oc = Vector3(
            rayOrigin.x - sphereCenter.x,
            rayOrigin.y - sphereCenter.y,
            rayOrigin.z - sphereCenter.z
        );

        // 二次方程式の係数を計算
        float a = rayDirection.x * rayDirection.x + 
                  rayDirection.y * rayDirection.y + 
                  rayDirection.z * rayDirection.z;
        
        float b = 2.0f * (oc.x * rayDirection.x + 
                          oc.y * rayDirection.y + 
                          oc.z * rayDirection.z);
        
        float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - sphereRadius * sphereRadius;

        // 判別式
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant < 0.0f) {
            return false;  // 交差していない
        }

        // 2つの解を計算（レイとスフィアの交点）
        float sqrtDiscriminant = std::sqrt(discriminant);
        float t1 = (-b - sqrtDiscriminant) / (2.0f * a);
        float t2 = (-b + sqrtDiscriminant) / (2.0f * a);

        // より近い交点を選択（ただし、負の値は無視）
        if (t1 > 0.0f) {
            distance = t1;
            return true;
        } else if (t2 > 0.0f) {
            distance = t2;
            return true;
        }

        return false;  // レイの後方にある
    }

    bool ObjectSelector::RayIntersectsTriangle(const Vector3& rayOrigin, const Vector3& rayDirection,
                                                const Vector3& v0, const Vector3& v1, const Vector3& v2,
                                                float& distance)
    {
        const float kEpsilon = 0.0000001f;

        // エッジベクトル
        Vector3 edge1 = Vector3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
        Vector3 edge2 = Vector3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);

        // 外積: rayDirection × edge2
        Vector3 h = Vector3(
            rayDirection.y * edge2.z - rayDirection.z * edge2.y,
            rayDirection.z * edge2.x - rayDirection.x * edge2.z,
            rayDirection.x * edge2.y - rayDirection.y * edge2.x
        );

        // 内積: edge1 · h
        float a = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;

        // レイが三角形の平面に平行な場合
        if (a > -kEpsilon && a < kEpsilon) {
            return false;
        }

        float f = 1.0f / a;
        Vector3 s = Vector3(rayOrigin.x - v0.x, rayOrigin.y - v0.y, rayOrigin.z - v0.z);
        
        // u座標を計算
        float u = f * (s.x * h.x + s.y * h.y + s.z * h.z);
        if (u < 0.0f || u > 1.0f) {
            return false;
        }

        // 外積: s × edge1
        Vector3 q = Vector3(
            s.y * edge1.z - s.z * edge1.y,
            s.z * edge1.x - s.x * edge1.z,
            s.x * edge1.y - s.y * edge1.x
        );

        // v座標を計算
        float v = f * (rayDirection.x * q.x + rayDirection.y * q.y + rayDirection.z * q.z);
        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }

        // 交点までの距離を計算
        float t = f * (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);

        if (t > kEpsilon) {
            distance = t;
            return true;
        }

        return false;
    }

    bool ObjectSelector::RayIntersectsMesh(const Vector3& rayOrigin, const Vector3& rayDirection,
                                            GameObject* object, float& distance)
    {
        // GameObjectからModelを取得
        Model* model = object->GetModel();
        if (!model || !model->IsInitialized()) {
            // モデルがない場合はフォールバック（バウンディングスフィア）
            const WorldTransform& transform = object->GetTransform();
            float radius = (std::max)({ transform.scale.x, transform.scale.y, transform.scale.z });
            radius = (std::max)(radius, 1.0f);
            Vector3 objectPosition = object->GetWorldPosition();
            return RayIntersectsSphere(rayOrigin, rayDirection, objectPosition, radius, distance);
        }

        // ModelResourceを取得
        const ModelResource* modelResource = model->GetModelResource();
        if (!modelResource || !modelResource->IsLoaded()) {
            // リソースがない場合はフォールバック
            const WorldTransform& transform = object->GetTransform();
            float radius = (std::max)({ transform.scale.x, transform.scale.y, transform.scale.z });
            radius = (std::max)(radius, 1.0f);
            Vector3 objectPosition = object->GetWorldPosition();
            return RayIntersectsSphere(rayOrigin, rayDirection, objectPosition, radius, distance);
        }

        // ModelDataを取得
        const ModelData& modelData = modelResource->GetModelData();
        
        // 頂点データとインデックスデータを取得
        const std::vector<VertexData>& vertices = modelData.vertices;
        const std::vector<int32_t>& indices = modelData.indices;

        if (vertices.empty() || indices.empty()) {
            // メッシュデータがない場合はフォールバック
            const WorldTransform& transform = object->GetTransform();
            float radius = (std::max)({ transform.scale.x, transform.scale.y, transform.scale.z });
            radius = (std::max)(radius, 1.0f);
            Vector3 objectPosition = object->GetWorldPosition();
            return RayIntersectsSphere(rayOrigin, rayDirection, objectPosition, radius, distance);
        }

        // オブジェクトのワールド変換行列を取得
        const WorldTransform& transform = object->GetTransform();
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();

        float closestDistance = (std::numeric_limits<float>::max)();
        bool hit = false;

        // 全ての三角形をチェック
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

            // ローカル空間の頂点座標を取得（Vector4からVector3に変換）
            Vector3 v0Local = Vector3(
                vertices[idx0].position.x,
                vertices[idx0].position.y,
                vertices[idx0].position.z
            );
            Vector3 v1Local = Vector3(
                vertices[idx1].position.x,
                vertices[idx1].position.y,
                vertices[idx1].position.z
            );
            Vector3 v2Local = Vector3(
                vertices[idx2].position.x,
                vertices[idx2].position.y,
                vertices[idx2].position.z
            );

            // ワールド空間に変換
            Vector3 v0World = TransformPoint(v0Local, worldMatrix);
            Vector3 v1World = TransformPoint(v1Local, worldMatrix);
            Vector3 v2World = TransformPoint(v2Local, worldMatrix);

            // レイと三角形の交差判定
            float t;
            if (RayIntersectsTriangle(rayOrigin, rayDirection, v0World, v1World, v2World, t)) {
                if (t < closestDistance) {
                    closestDistance = t;
                    hit = true;
                }
            }
        }

        if (hit) {
            distance = closestDistance;
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
                                                const ICamera* camera, const Vector2& mousePos)
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
