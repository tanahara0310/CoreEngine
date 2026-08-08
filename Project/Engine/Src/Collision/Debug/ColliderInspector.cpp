#include "pch.h"
#include "ColliderInspector.h"

#ifdef USE_IMGUI

#include "Collision/ColliderComponent.h"
#include "GameObject/GameObject.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <string>

namespace CoreEngine
{
namespace ColliderInspector
{
    namespace {
        /// @brief CollisionLayer の表示名（enum の並びと必ず一致させること）
        const char* const kLayerNames[] = {
            "Default", "Player", "Enemy", "PlayerBullet", "EnemyBullet",
            "Boss", "BossBullet", "BossAttack", "Item", "Environment",
        };
        static_assert(
            static_cast<int>(CollisionLayer::Count) == static_cast<int>(std::size(kLayerNames)),
            "CollisionLayer を増減したら kLayerNames も更新すること");

        const char* const kShapeNames[] = { "Sphere", "Box" };
        static_assert(
            static_cast<int>(ColliderShapeType::Count) == static_cast<int>(std::size(kShapeNames)),
            "ColliderShapeType を増減したら kShapeNames も更新すること");

        /// @brief 1 本ぶんの編集 UI
        bool DrawOne(Collider& collider, int index)
        {
            bool changed = false;
            ImGui::PushID(index);

            const CollisionShape& shape = collider.GetShape();
            const char* shapeName = kShapeNames[static_cast<int>(shape.type)];

            const std::string header =
                "[" + std::to_string(index) + "] " + shapeName
                + " / " + kLayerNames[static_cast<int>(collider.GetLayer())];

            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

                // ── 形状 ────────────────────────────────────────
                int shapeIndex = static_cast<int>(shape.type);
                if (ImGui::Combo("形状", &shapeIndex, kShapeNames,
                        static_cast<int>(std::size(kShapeNames)))) {
                    CollisionShape next = shape;
                    next.type = static_cast<ColliderShapeType>(shapeIndex);
                    collider.SetShape(next);
                    changed = true;
                }

                if (shape.type == ColliderShapeType::Sphere) {
                    float radius = shape.radius;
                    if (ImGui::DragFloat("半径", &radius, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetRadius(radius);
                        changed = true;
                    }
                } else {
                    Vector3 size = shape.size;
                    if (ImGui::DragFloat3("サイズ", &size.x, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetSize(size);
                        changed = true;
                    }
                }

                Vector3 offset = shape.offset;
                if (ImGui::DragFloat3("オフセット", &offset.x, 0.01f)) {
                    collider.SetOffset(offset);
                    changed = true;
                }
                UI::Hint("オーナー原点からのローカル位置。スケールが乗る。");

                // ── レイヤー / フラグ ───────────────────────────
                int layerIndex = static_cast<int>(collider.GetLayer());
                if (ImGui::Combo("レイヤー", &layerIndex, kLayerNames,
                        static_cast<int>(std::size(kLayerNames)))) {
                    collider.SetLayer(static_cast<CollisionLayer>(layerIndex));
                    changed = true;
                }

                bool enabled = collider.IsEnabled();
                if (ImGui::Checkbox("有効", &enabled)) {
                    collider.SetEnabled(enabled);
                    changed = true;
                }

                bool isTrigger = collider.IsTrigger();
                ImGui::SameLine();
                if (ImGui::Checkbox("トリガー", &isTrigger)) {
                    collider.SetTrigger(isTrigger);
                    changed = true;
                }
                UI::Hint("トリガー = 通知だけで押し出さない。両方外すとめり込みを解消する。");

                bool isStatic = collider.IsStatic();
                if (ImGui::Checkbox("静止", &isStatic)) {
                    collider.SetStatic(isStatic);
                    changed = true;
                }
                UI::Hint("静止同士は判定しない。押し出しでも動かない側になる。");

                // ── 実効値（スケール適用後） ────────────────────
                UI::SectionHeader("実効値");
                const Geometry::AABB bounds = collider.GetWorldAABB();
                const Vector3 center = bounds.GetCenter();
                const Vector3 extent = bounds.GetSize();
                UI::HintF("中心 (%.2f, %.2f, %.2f)", center.x, center.y, center.z);
                UI::HintF("AABB (%.2f, %.2f, %.2f)", extent.x, extent.y, extent.z);

                ImGui::TreePop();
            }

            ImGui::PopID();
            return changed;
        }
    }

    bool Draw(GameObject& object)
    {
        bool changed = false;

        // 表示は TryGetColliders()（生成しない）で行う。GetColliders() を使うと
        // インスペクタを開いただけで全オブジェクトに ColliderComponent が生える。
        ColliderComponent* colliders = object.TryGetColliders();

        UI::SectionHeader("コライダー");

        if (!colliders || colliders->IsEmpty()) {
            UI::Hint("コライダーがありません");
        } else {
            for (size_t i = 0; i < colliders->Count(); ++i) {
                if (Collider* collider = colliders->Get(i)) {
                    changed |= DrawOne(*collider, static_cast<int>(i));
                }
            }
        }

        UI::Separator();

        // 追加ボタンだけはオンデマンド生成でよい（押した時点で要ると確定している）
        if (ImGui::Button("球を追加")) {
            object.GetColliders().AddSphere(0.5f);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("箱を追加")) {
            object.GetColliders().AddBox({ 1.0f, 1.0f, 1.0f });
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("すべて削除")) {
            if (colliders) {
                colliders->RemoveAll();
            }
            changed = true;
        }

        UI::Hint("ワイヤ表示は Engine Settings の r.Collision.DebugDraw で切り替える。");
        return changed;
    }
}
}

#endif // USE_IMGUI
