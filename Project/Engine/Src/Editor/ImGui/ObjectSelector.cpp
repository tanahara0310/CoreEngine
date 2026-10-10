#include "pch.h"
#include "ObjectSelector.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/GameObjectManager.h"
#include "Camera/Camera.h"
#include "WinApp/WinApp.h"
#include "Graphics/Render/RenderPassType.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/ScenePicking.h"

namespace CoreEngine
{
    void ObjectSelector::Initialize(const GameObjectManager* objects)
    {
        objects_ = objects;
        selectedObjectId_ = {};
        selectedSpriteId_ = {};
        gizmoMode_ = Gizmo::Mode::Translate;
    }

    GameObject* ObjectSelector::GetSelectedObject() const
    {
        return Resolve(selectedObjectId_);
    }

    GameObject* ObjectSelector::GetSelectedSprite() const
    {
        return Resolve(selectedSpriteId_);
    }

    void ObjectSelector::SelectObject(GameObject* object)
    {
        selectedObjectId_ = object ? object->GetObjectId() : ObjectId{};
        selectedSpriteId_ = {};
    }

    void ObjectSelector::SelectSprite(GameObject* sprite)
    {
        selectedSpriteId_ = sprite ? sprite->GetObjectId() : ObjectId{};
        selectedObjectId_ = {};
    }

    GameObject* ObjectSelector::Resolve(ObjectId id) const
    {
        return (objects_ && id.IsValid()) ? objects_->FindObject(id) : nullptr;
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
                    viewportSelection_ = true;
                } else {
                    ClearSelection();
                }
            }
        }

        UpdateGizmoShortcut(isViewportHovered);
    }

    void ObjectSelector::UpdateGizmoShortcut(bool isViewportHovered)
    {
        // 他の窓を触っている最中に切り替わらないよう、ビューポートの上でだけ見る
        if (!isViewportHovered || !input_) {
            return;
        }
        struct Shortcut { const char* actionId; Gizmo::Mode mode; };
        static constexpr Shortcut kShortcuts[] = {
            { "EditorGizmoTranslate", Gizmo::Mode::Translate },
            { "EditorGizmoRotate",    Gizmo::Mode::Rotate },
            { "EditorGizmoScale",     Gizmo::Mode::Scale },
        };
        for (const Shortcut& shortcut : kShortcuts) {
            const InputAction action = InputActionFromString(shortcut.actionId);
            if (action != InputAction::Invalid && input_->IsActionTriggered(action)) {
                SetGizmoMode(shortcut.mode);
            }
        }
    }

    void ObjectSelector::DrawGizmo(const Camera* camera)
    {
        GameObject* const selected = GetSelectedObject();
        if (selected && camera) {
            // ギズモはトランスフォームを持つものにだけ出る。持たないもの（UI など）では、
            // 別の窓のギズモの操作を自分の操作と取り違えないように何もしない
            ITransformSource* const source = selected->GetComponent<ITransformSource>();
            if (!source) {
                return;
            }
            if (!Gizmo::IsUsing()) {
                beforeGizmoTranslate_ = source->GetTranslate();
                beforeGizmoRotate_ = source->GetRotate();
                beforeGizmoScale_ = source->GetScale();
                beforeGizmoActive_ = selected->IsActive();
            }

            Gizmo::Manipulate(selected, camera, gizmoMode_);

            // ギズモ操作中→操作完了の遷移を検出
            bool isUsing = Gizmo::IsUsing();
            if (wasGizmoUsing_ && !isUsing) {
                if (onTransformChanged_) {
                    onTransformChanged_(selected);
                }
                if (onGizmoEditCommitted_) {
                    onGizmoEditCommitted_(selected,
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
                GameObject* hitSprite = RaycastSprite(gameObjectManager, camera, mousePos);
                if (hitSprite) {
                    SelectSprite(hitSprite);
                } else {
                    // スプライト選択のみクリア（3Dオブジェクトの選択はUpdate()側で管理する）
                    selectedSpriteId_ = {};
                }
            }
        }

        UpdateGizmoShortcut(isViewportHovered);
    }

    void ObjectSelector::DrawGizmo2D(const Camera* camera)
    {
        GameObject* const selected = GetSelectedSprite();
        if (selected && camera) {
            // ギズモ非使用中は操作前スナップショットを連続更新する
            if (!Gizmo::IsUsing()) {
                if (auto* source = selected->GetComponent<ITransformSource>()) {
                    beforeGizmoTranslate_ = source->GetTranslate();
                    beforeGizmoRotate_ = source->GetRotate();
                    beforeGizmoScale_ = source->GetScale();
                }
                beforeGizmoActive_ = selected->IsActive();
            }

            Gizmo::Manipulate2D(selected, camera, gizmoMode_);

            // ギズモ操作中→操作完了の遷移を検出
            bool isUsing = Gizmo::IsUsing();
            if (wasGizmoUsing_ && !isUsing) {
                if (onTransformChanged_) {
                    onTransformChanged_(selected);
                }
                if (onGizmoEditCommitted_) {
                    onGizmoEditCommitted_(selected,
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

        // スクリーンサイズ（2D カメラの正射影は基準解像度に固定されている）
        const Vector2 screenSize = {
            static_cast<float>(WinApp::kReferenceWidth),
            static_cast<float>(WinApp::kReferenceHeight)
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

    GameObject* ObjectSelector::RaycastSprite(GameObjectManager* gameObjectManager,
        const Camera* camera, const Vector2& mousePos)
    {
        // マウス位置をワールド座標に変換
        Vector2 worldMousePos = ScreenToWorld2D(mousePos, camera);

        const auto& objects = gameObjectManager->GetAllObjects();
        GameObject* closestSprite = nullptr;
        int highestOrder = INT_MIN;

        // スプライトオブジェクトのみをチェック
        for (const auto& obj : objects) {
            if (!obj->IsActive() || !gameObjectManager->IsShownInIsolation(*obj)) {
                continue;
            }

            // スプライトオブジェクトかどうかをチェック
            if (obj->GetRenderPassType() != RenderPassType::Sprite) {
                continue;
            }

            auto* sprite = obj->GetComponent<SpriteRendererComponent>();
            auto* source = obj->GetComponent<ITransformSource>();
            if (!sprite || !source) {
                continue;
            }

            // スプライトの矩形との当たり判定
            const Vector3 translate = source->GetTranslate();
            const Vector3 scale = source->GetScale();
            Vector2 textureSize = sprite->GetTextureSize();
            Vector2 anchor = sprite->GetAnchor();

            // スプライトの実際のサイズを計算
            float actualWidth = textureSize.x * scale.x;
            float actualHeight = textureSize.y * scale.y;

            // アンカーポイントを考慮した矩形の範囲を計算
            float left = translate.x - anchor.x * actualWidth;
            float right = translate.x + (1.0f - anchor.x) * actualWidth;
            float bottom = translate.y - anchor.y * actualHeight;
            float top = translate.y + (1.0f - anchor.y) * actualHeight;

            // 矩形内にマウスがあるかチェック
            if (worldMousePos.x >= left && worldMousePos.x <= right &&
                worldMousePos.y >= bottom && worldMousePos.y <= top) {
                // 最前面（renderOrder が最大）のスプライトを優先選択
                int order = sprite->GetSortingLayer() * 1000 + sprite->GetOrderInLayer();
                if (order > highestOrder) {
                    highestOrder = order;
                    closestSprite = obj.get();
                }
            }
        }

        return closestSprite;
    }

    GameObject* ObjectSelector::RaycastObject(GameObjectManager* gameObjectManager,
        const Camera* camera, const Vector2& mousePos)
    {
        const Editor::ScenePicking::Ray ray = Editor::ScenePicking::ScreenToRay(mousePos, *camera);
        const std::optional<Editor::ScenePicking::Hit> hit =
            Editor::ScenePicking::Raycast(*gameObjectManager, ray, Editor::ScenePicking::Fallback::Sphere);
        return hit ? hit->object : nullptr;
    }
}
