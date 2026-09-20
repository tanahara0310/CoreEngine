#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "SceneSettingsPanel.h"

#ifdef CORE_EDITOR

#include "Collision/CollisionConfig.h"
#include "Collision/CollisionLayer.h"
#include "EngineSystem/EngineSystem.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Scene/BaseScene.h"
#include "Scene/Feature/CollisionFeature.h"
#include "Scene/Feature/GroundFeature.h"
#include "Scene/Feature/SceneFeatureRegistry.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <string>
#include <vector>

namespace CoreEngine
{
namespace SceneSettingsPanel
{
    namespace {
        /// 描くたびにシーンを引き直すための、エンジンへの参照（プロセスの寿命）
        EngineSystem* s_engine = nullptr;

        constexpr int kLayerCount = static_cast<int>(CollisionLayer::Count);

        /// @brief 今のシーン（無ければ nullptr）
        BaseScene* ResolveScene()
        {
            SceneManager* const manager = s_engine ? s_engine->GetSceneManager() : nullptr;
            return manager ? dynamic_cast<BaseScene*>(manager->GetCurrentScene()) : nullptr;
        }

        /// @brief 未保存の印を付ける（Ctrl+S の対象になる）
        void MarkDirty()
        {
            if (SceneDebugEditor* const editor = Editor::SceneAccess::SceneEditor()) {
                editor->MarkSceneDirty();
            }
        }

        /// @brief このシーンが持つ Feature を並べる（保存データから足せるものには印を付ける）
        void DrawFeatures(const BaseScene& scene)
        {
            const std::vector<std::string> addable = SceneFeatureRegistry::GetNames();
            for (const char* const name : scene.GetFeatureNames()) {
                if (!name) {
                    continue;
                }
                const bool fromData =
                    std::find(addable.begin(), addable.end(), name) != addable.end();
                ImGui::BulletText("%s%s", name, fromData ? "（保存データに書かれる）" : "");
            }
            UI::Hint("印の無いものは、どのシーンにも入る既定の Feature です。");
        }

        /// @brief 既定の床を使うか
        void DrawGround(BaseScene& scene)
        {
            auto* const ground = scene.GetFeature<GroundFeature>();
            if (!ground) {
                return;
            }

            bool enabled = !ground->IsSuppressed();
            if (ImGui::Checkbox("既定の床を使う", &enabled)) {
                ground->SetSuppressed(!enabled);
                MarkDirty();
            }
            UI::Hint("床の生成はシーンを組むときに決まるので、切り替えは次に開いたときに効きます。");
        }

        /// @brief レイヤー同士が当たるかの表
        void DrawCollisionMatrix(BaseScene& scene)
        {
            auto* const collision = scene.GetFeature<CollisionFeature>();
            if (!collision) {
                return;
            }
            CollisionConfig& config = collision->GetConfig();

            UI::Hint("チェックが入っている組み合わせだけ衝突判定が走ります。");

            // 対称行列なので下三角だけ描く（同じ組み合わせが 2 回出ない）
            if (ImGui::BeginTable("CollisionMatrix", kLayerCount + 1,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit
                    | ImGuiTableFlags_ScrollX)) {

                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                for (int col = 0; col < kLayerCount; ++col) {
                    ImGui::TableSetupColumn(kCollisionLayerNames[col], ImGuiTableColumnFlags_WidthFixed, 26.0f);
                }

                // 縦書き風のヘッダは組めないので、番号 + 凡例で代用する
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                UI::Hint("行 \\ 列");
                for (int col = 0; col < kLayerCount; ++col) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", col);
                }

                for (int row = 0; row < kLayerCount; ++row) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::Text("%d %s", row, kCollisionLayerNames[row]);

                    for (int col = 0; col < kLayerCount; ++col) {
                        ImGui::TableNextColumn();
                        if (col > row) {
                            continue;   // 上三角は対称なので描かない
                        }

                        ImGui::PushID(row * kLayerCount + col);
                        bool enabled = config.IsCollisionEnabled(
                            static_cast<CollisionLayer>(row), static_cast<CollisionLayer>(col));
                        if (ImGui::Checkbox("##cell", &enabled)) {
                            config.SetCollisionEnabled(
                                static_cast<CollisionLayer>(row),
                                static_cast<CollisionLayer>(col), enabled);
                            MarkDirty();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s x %s",
                                kCollisionLayerNames[row], kCollisionLayerNames[col]);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }

        void Draw()
        {
            BaseScene* const scene = ResolveScene();
            if (!scene) {
                UI::Hint("シーンがありません");
                return;
            }

            UI::Hint("ここの設定は Ctrl+S でシーン（_scene.json）と一緒に保存されます。");
            UI::Separator();

            if (ImGui::CollapsingHeader("Feature", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawFeatures(*scene);
            }
            if (ImGui::CollapsingHeader("床", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawGround(*scene);
            }
            if (ImGui::CollapsingHeader("当たり判定のレイヤー", ImGuiTreeNodeFlags_DefaultOpen)) {
                DrawCollisionMatrix(*scene);
            }
        }
    }

    void EnsureRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }
        s_engine = engine;

        Editor::EditorPanelRegistry::Get().Register({
            .id = "Scene Settings",
            .placement = Editor::PanelPlacement::SettingsSection,
            .draw = [] { Draw(); },
            });

        registered = true;
    }
}
}

#endif // CORE_EDITOR
