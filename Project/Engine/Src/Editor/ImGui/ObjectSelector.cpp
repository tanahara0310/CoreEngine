#include "pch.h"
#include "ObjectSelector.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/GameObjectManager.h"
#include "Camera/Camera.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/ScenePicking.h"

namespace CoreEngine
{
    void ObjectSelector::Initialize(const GameObjectManager* objects)
    {
        objects_ = objects;
        selectedObjectId_ = {};
        gizmoMode_ = Gizmo::Mode::Translate;
    }

    GameObject* ObjectSelector::GetSelectedObject() const
    {
        return Resolve(selectedObjectId_);
    }

    void ObjectSelector::SelectObject(GameObject* object)
    {
        selectedObjectId_ = object ? object->GetObjectId() : ObjectId{};
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

    GameObject* ObjectSelector::RaycastObject(GameObjectManager* gameObjectManager,
        const Camera* camera, const Vector2& mousePos)
    {
        const Editor::ScenePicking::Ray ray = Editor::ScenePicking::ScreenToRay(mousePos, *camera);
        const std::optional<Editor::ScenePicking::Hit> hit =
            Editor::ScenePicking::Raycast(*gameObjectManager, ray, Editor::ScenePicking::Fallback::Sphere);
        return hit ? hit->object : nullptr;
    }
}
