#include "pch.h"
#include "ColliderInspector.h"

#ifdef CORE_EDITOR

#include "Collision/ColliderComponent.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/GameObject.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/DragDropPayload.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/ModelData.h"
#include "Physics/RigidbodyComponent.h"

#include <string>

namespace CoreEngine
{
namespace ColliderInspector
{
    namespace {
        // 名前はプロジェクト設定が持つので、描くたびに今の表を引く
        const std::vector<std::string>& LayerNames() { return CollisionLayers::Names(); }
        const auto& kShapeNames = kColliderShapeTypeNames;

        /// @brief スプライトの板に重なる箱の大きさと中心（どちらもスケールを掛ける前のローカル）
        /// @param depth 箱の奥行き
        /// @return スプライトが無ければ false
        bool FitBoxToSprite(const GameObject& object, float depth, Vector3& outSize, Vector3& outOffset)
        {
            const auto* const sprite = object.GetComponent<SpriteRendererComponent>();
            if (!sprite) {
                return false;
            }
            const Vector2 size = sprite->GetSize();
            const Vector2 anchor = sprite->GetAnchor();
            outSize = { size.x, size.y, depth };
            outOffset = { (0.5f - anchor.x) * size.x, (anchor.y - 0.5f) * size.y, 0.0f };
            return true;
        }

        /// @brief 三角形の取り出し方の表示名（CollisionMeshSource の並びと同じ順）
        constexpr const char* kMeshSourceLabels[] = { "同じオブジェクトのモデル", "モデルのファイル" };
        static_assert(std::size(kMeshSourceLabels) == static_cast<std::size_t>(CollisionMeshSource::Count));

        /// @brief 三角形を取るモデルのファイルを選ぶ欄（一覧と ProjectView からのドロップ）
        bool DrawMeshModelPicker(Collider& collider)
        {
            CollisionShape next = collider.GetShape();
            bool changed = false;

            const char* const preview = next.meshModel.empty() ? "（なし）" : next.meshModel.c_str();
            if (ImGui::BeginCombo("モデル", preview)) {
                for (const AssetInfo* info : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Model)) {
                    const std::string path = ToAssetPath(*info);
                    if (ImGui::Selectable(path.c_str(), path == next.meshModel) && path != next.meshModel) {
                        next.meshModel = path;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto fileName = UI::AcceptPayload<std::string>("MODEL_FILE")) {
                    if (const AssetInfo* dropped = FindAssetInfo(*fileName, AssetType::Model)) {
                        next.meshModel = ToAssetPath(*dropped);
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            UI::Hint("ProjectView からモデルのファイルを落としても選べる。");

            if (changed) {
                collider.SetShape(next);
            }
            return changed;
        }

        /// @brief メッシュの形の設定と状態
        bool DrawMeshSettings(GameObject& object, Collider& collider)
        {
            bool changed = false;
            CollisionShape next = collider.GetShape();

            if (InspectorLayout::EnumCombo("三角形", next.meshSource, kMeshSourceLabels)) {
                collider.SetShape(next);
                changed = true;
            }
            if (next.meshSource == CollisionMeshSource::File) {
                changed |= DrawMeshModelPicker(collider);
                next = collider.GetShape();
            }

            int lod = next.meshLod;
            if (ImGui::SliderInt("LOD", &lod, 0, static_cast<int>(SubMeshData::kMaxLodCount) - 1)) {
                next.meshLod = lod;
                collider.SetShape(next);
                changed = true;
            }
            UI::Hint("0 が原型。大きい段ほど三角形が少なく軽い。モデルに無い段はいちばん簡単な段になる。");

            bool oneSided = next.meshOneSided;
            if (ImGui::Checkbox("表からだけ当たる", &oneSided)) {
                next.meshOneSided = oneSided;
                collider.SetShape(next);
                changed = true;
            }
            UI::Hint("裏から入った物は押し返さない（下からすり抜けられる床など）。");

            // 状態と大きさ
            collider.RefreshMesh();
            if (const Geometry::TriangleMesh* const mesh = collider.GetMesh()) {
                const Geometry::TriangleMeshStats& stats = mesh->Stats();
                UI::HintF("三角形 %u（つぶれていて外した %u）・頂点 %u・%.1f MB・作るのに %.1f ms",
                    stats.triangles, stats.removedTriangles, stats.vertices,
                    static_cast<double>(stats.memoryBytes) / (1024.0 * 1024.0),
                    static_cast<double>(stats.buildMilliseconds));
            } else {
                ImGui::TextColored(Editor::Theme::kWarn, "%s", ToDisplayText(collider.GetMeshStatus()));
            }

            const auto* const body = object.GetComponent<RigidbodyComponent>();
            if (body && body->IsDynamic()) {
                ImGui::TextColored(Editor::Theme::kWarn, "メッシュの形は動く剛体には使えません（物理はこの形を使いません）");
                UI::Hint("剛体には球・箱・カプセルを組み合わせてください。");
            }
            return changed;
        }

        /// @brief 1 本ぶんの編集 UI
        bool DrawOne(GameObject& object, Collider& collider, int index)
        {
            bool changed = false;
            ImGui::PushID(index);

            const CollisionShape& shape = collider.GetShape();
            const char* shapeName = kShapeNames[static_cast<int>(shape.type)];

            const std::string header =
                "[" + std::to_string(index) + "] " + shapeName
                + " / " + ToString(collider.GetLayer());

            if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

                // ── 形状 ────────────────────────────────────────
                CollisionShape next = shape;
                if (InspectorLayout::EnumCombo("形状", next.type, kShapeNames)) {
                    collider.SetShape(next);
                    // メッシュは止まっている物の形に当てるので、押し出す静止の形にする
                    if (next.type == ColliderShapeType::Mesh) {
                        collider.SetTrigger(false);
                        collider.SetStatic(true);
                    }
                    changed = true;
                }

                if (shape.type == ColliderShapeType::Sphere) {
                    float radius = shape.radius;
                    if (ImGui::DragFloat("半径", &radius, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetRadius(radius);
                        changed = true;
                    }
                } else if (shape.type == ColliderShapeType::Capsule) {
                    float radius = shape.radius;
                    if (ImGui::DragFloat("半径", &radius, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetRadius(radius);
                        changed = true;
                    }
                    float height = shape.height;
                    if (ImGui::DragFloat("高さ", &height, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetHeight(height);
                        changed = true;
                    }
                } else if (shape.type == ColliderShapeType::Mesh) {
                    changed |= DrawMeshSettings(object, collider);
                } else {
                    Vector3 size = shape.size;
                    if (ImGui::DragFloat3("サイズ", &size.x, 0.01f, 0.0f, 1000.0f)) {
                        collider.SetSize(size);
                        changed = true;
                    }
                    Vector3 fittedSize{};
                    Vector3 fittedOffset{};
                    if (FitBoxToSprite(object, shape.size.z, fittedSize, fittedOffset)
                        && ImGui::Button("画像に合わせる")) {
                        collider.SetSize(fittedSize);
                        collider.SetOffset(fittedOffset);
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
                // 名前の数は設定で変わるので、その場で引いて並べる
                if (ImGui::BeginCombo("レイヤー", ToString(collider.GetLayer()).c_str())) {
                    const std::vector<std::string>& names = LayerNames();
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        const auto layer = static_cast<CollisionLayer>(i);
                        if (ImGui::Selectable(names[i].c_str(), collider.GetLayer() == layer)) {
                            collider.SetLayer(layer);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
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

        // 表示は付いているものだけを見る（生成しない）。
        // 取得のついでに足すと、インスペクタを開いただけで全オブジェクトにコライダーが生える
        ColliderComponent* colliders = object.GetComponent<ColliderComponent>();

        UI::SectionHeader("コライダー");

        if (!colliders || colliders->IsEmpty()) {
            UI::Hint("コライダーがありません");
        } else {
            for (size_t i = 0; i < colliders->Count(); ++i) {
                if (Collider* collider = colliders->Get(i)) {
                    changed |= DrawOne(object, *collider, static_cast<int>(i));
                }
            }
        }

        UI::Separator();

        // 追加ボタンだけはオンデマンド生成でよい（押した時点で要ると確定している）
        if (ImGui::Button("球を追加")) {
            object.GetOrAddComponent<ColliderComponent>()->AddSphere(0.5f);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("箱を追加")) {
            // スプライトがあれば、その板に重なる大きさで足す
            Vector3 size{ 1.0f, 1.0f, 1.0f };
            Vector3 offset{};
            FitBoxToSprite(object, 1.0f, size, offset);
            object.GetOrAddComponent<ColliderComponent>()->AddBox(size, CollisionLayer::Default, offset);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("メッシュを追加")) {
            object.GetOrAddComponent<ColliderComponent>()->AddMesh();
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("すべて削除")) {
            if (colliders) {
                colliders->RemoveAll();
            }
            changed = true;
        }

        UI::Hint("ワイヤ表示はツールバーの Collider（d.Collision.DebugDraw）で切り替える。");
        return changed;
    }
}
}

#endif // CORE_EDITOR
