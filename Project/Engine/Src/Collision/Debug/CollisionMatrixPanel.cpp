#include "pch.h"
#include "CollisionMatrixPanel.h"

#ifdef USE_IMGUI

#include "Collision/CollisionConfig.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/ImGuiAll.h"

namespace CoreEngine
{
namespace CollisionMatrixPanel
{
    namespace {
        /// 編集対象。シーンの CollisionFeature が Initialize / Finalize で差し替える。
        CollisionConfig* s_activeConfig = nullptr;

        /// @brief CollisionLayer の表示名（enum の並びと必ず一致させること）
        const char* const kLayerNames[] = {
            "Default", "Player", "Enemy", "PlayerBullet", "EnemyBullet",
            "Boss", "BossBullet", "BossAttack", "Item", "Environment",
        };
        static_assert(
            static_cast<int>(CollisionLayer::Count) == static_cast<int>(std::size(kLayerNames)),
            "CollisionLayer を増減したら kLayerNames も更新すること");

        constexpr int kLayerCount = static_cast<int>(CollisionLayer::Count);

        void DrawMatrix()
        {
            if (!s_activeConfig) {
                UI::Hint("シーンが初期化されていません");
                return;
            }

            UI::Hint("チェックが入っている組み合わせだけ衝突判定が走ります。");
            UI::Hint("この編集は保存されません。確定した設定はシーンの OnInitialize() へ書いてください。");
            UI::Separator();

            // 対称行列なので下三角だけ描く（同じ組み合わせが 2 回出ない）
            if (ImGui::BeginTable("CollisionMatrix", kLayerCount + 1,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit
                    | ImGuiTableFlags_ScrollX)) {

                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                for (int col = 0; col < kLayerCount; ++col) {
                    ImGui::TableSetupColumn(kLayerNames[col], ImGuiTableColumnFlags_WidthFixed, 26.0f);
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
                    ImGui::Text("%d %s", row, kLayerNames[row]);

                    for (int col = 0; col < kLayerCount; ++col) {
                        ImGui::TableNextColumn();
                        if (col > row) {
                            continue;   // 上三角は対称なので描かない
                        }

                        ImGui::PushID(row * kLayerCount + col);
                        bool enabled = s_activeConfig->IsCollisionEnabled(
                            static_cast<CollisionLayer>(row), static_cast<CollisionLayer>(col));
                        if (ImGui::Checkbox("##cell", &enabled)) {
                            s_activeConfig->SetCollisionEnabled(
                                static_cast<CollisionLayer>(row),
                                static_cast<CollisionLayer>(col), enabled);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s x %s", kLayerNames[row], kLayerNames[col]);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    void SetActiveConfig(CollisionConfig* config)
    {
        s_activeConfig = config;
    }

    void EnsureRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        auto* debug = engine->GetDebugSubsystem();
        auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
        if (!gameDebugUI) {
            return;
        }

        // ドロワーは何もキャプチャしない（ファイルスコープの s_activeConfig を読むだけ）
        gameDebugUI->RegisterEnginePanel("Collision Matrix", [] { DrawMatrix(); },
            EnginePanelCategory::Settings);

        registered = true;
    }
}
}

#endif // USE_IMGUI
