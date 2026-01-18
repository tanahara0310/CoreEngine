#include "BossAIDebugUI.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/AI/BossAI.h"
#include "Application/AL3/AI/BehaviorTree/BehaviorNode.h"
#include "Application/AL3/AI/BehaviorTree/CompositeNode.h"
#include "Application/AL3/AI/BehaviorTree/BehaviorTreeNodes.h"
#include "Application/AL3/AI/Action/BossActionManager.h"
#include "Application/AL3/AI/Action/BossAction.h"
#include <imgui.h>

using namespace CoreEngine;

void BossAIDebugUI::Initialize(BossObject* boss) {
    boss_ = boss;
}

void BossAIDebugUI::Update() {
    if (!isVisible_ || !boss_) {
        return;
    }

    ShowMainWindow();
}

void BossAIDebugUI::ShowMainWindow() {
    // ウィンドウサイズを設定
    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);
    
    ImGui::Begin(mainWindowName_, &isVisible_);

    auto ai = boss_->GetAI();
    if (!ai) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "AI が初期化されていません");
        ImGui::End();
        return;
    }

    // ===== セクション1: AI制御 =====
    if (ImGui::CollapsingHeader("AI制御", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        
        // AI有効/無効トグル
        bool aiEnabled = boss_->IsAIEnabled();
        if (ImGui::Checkbox("AI有効", &aiEnabled)) {
            boss_->SetAIEnabled(aiEnabled);
        }
        
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("AIの自動実行をON/OFFします");
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // ===== セクション2: 現在の状態 =====
    if (ImGui::CollapsingHeader("現在の状態", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        
        // 実行時間
        ImGui::Text("総実行時間:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%.2f 秒", ai->GetTotalTime());
        
        ImGui::Separator();
        
        // プレイヤーとの距離
        float distance = ai->GetDistanceToPlayer();
        ImGui::Text("プレイヤーとの距離:");
        ImGui::SameLine();
        
        // 距離に応じて色を変更
        ImVec4 distanceColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        const char* rangeText = "";
        if (distance < 3.0f) {
            distanceColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // 近距離 = 赤
            rangeText = " (近距離)";
        } else if (distance < 8.0f) {
            distanceColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // 中距離 = 黄
            rangeText = " (中距離)";
        } else {
            distanceColor = ImVec4(0.5f, 0.5f, 1.0f, 1.0f);  // 遠距離 = 青
            rangeText = " (遠距離)";
        }
        ImGui::TextColored(distanceColor, "%.2f%s", distance, rangeText);
        
        ImGui::Separator();
        
        // 現在のアクション
        ImGui::Text("現在のアクション:");
        std::string actionName = ai->GetCurrentActionName();
        ImVec4 actionColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        const char* actionNameJP = "なし";
        
        if (actionName == "Idle") {
            actionNameJP = "待機";
            actionColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        } else if (actionName == "Walk") {
            actionNameJP = "歩行";
            actionColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (actionName == "JumpAttack") {
            actionNameJP = "ジャンプ攻撃";
            actionColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
        } else if (actionName == "PunchAttack") {
            actionNameJP = "パンチ攻撃";
            actionColor = ImVec4(1.0f, 0.0f, 0.5f, 1.0f);
        }
        
        ImGui::SameLine();
        ImGui::TextColored(actionColor, "%s", actionNameJP);
        
        ImGui::Separator();
        
        // ビヘイビアツリーの状態
        ImGui::Text("ツリーステータス:");
        std::string status = ai->GetLastTreeStatus();
        ImVec4 statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        const char* statusTextJP = "不明";
        
        if (status == "Success") {
            statusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            statusTextJP = "成功";
        } else if (status == "Failure") {
            statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            statusTextJP = "失敗";
        } else if (status == "Running") {
            statusColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
            statusTextJP = "実行中";
        }
        
        ImGui::SameLine();
        ImGui::TextColored(statusColor, "%s", statusTextJP);

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // ===== セクション3: アクション詳細 =====
    if (ImGui::CollapsingHeader("アクション詳細", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        
        auto actionManager = ai->GetActionManager();
        if (actionManager) {
            auto currentAction = actionManager->GetCurrentAction();
            if (currentAction) {
                // アクション名（英語）
                ImGui::Text("アクション名:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", currentAction->GetName());
                
                // アクション状態
                auto state = currentAction->GetState();
                const char* stateTextJP = "不明";
                ImVec4 stateColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                
                switch (state) {
                    case BossAction::State::Idle:
                        stateTextJP = "待機中";
                        stateColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                        break;
                    case BossAction::State::Running:
                        stateTextJP = "実行中";
                        stateColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                        break;
                    case BossAction::State::Completed:
                        stateTextJP = "完了";
                        stateColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
                        break;
                }
                
                ImGui::Text("状態:");
                ImGui::SameLine();
                ImGui::TextColored(stateColor, "%s", stateTextJP);
                
                // プログレスバー
                if (state == BossAction::State::Running) {
                    float progress = fmodf(static_cast<float>(ImGui::GetTime()), 1.0f);
                    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "実行中...");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "アクションが実行されていません");
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            // 登録済みアクションリスト
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "登録済みアクション:");
            ImGui::Indent();
            ImGui::BulletText("待機 (Idle)");
            ImGui::BulletText("歩行 (Walk)");
            ImGui::BulletText("ジャンプ攻撃 (JumpAttack)");
            ImGui::BulletText("パンチ攻撃 (PunchAttack)");
            ImGui::Unindent();
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "アクションマネージャーが利用できません");
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // ===== セクション4: ビヘイビアツリー構造 =====
    if (ImGui::CollapsingHeader("ビヘイビアツリー構造")) {
        ImGui::Indent();
        
        auto rootNode = ai->GetBehaviorTreeRoot();
        if (rootNode) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "ツリー構造:");
            ImGui::Separator();
            
            // ツリーを描画
            DrawNodeTree(rootNode, "ルート (セレクター)", 0);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "ビヘイビアツリーが構築されていません");
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    // ===== セクション5: ヘルプ =====
    if (ImGui::CollapsingHeader("ヘルプ")) {
        ImGui::Indent();
        
        ImGui::TextWrapped("ビヘイビアツリー: AIの意思決定を行う階層構造");
        ImGui::Spacing();
        ImGui::TextWrapped("セレクター: 子ノードを優先度順に試行し、成功するまで実行");
        ImGui::Spacing();
        ImGui::TextWrapped("シーケンス: すべての子ノードを順番に実行");
        ImGui::Spacing();
        ImGui::TextWrapped("ランダム: 指定確率で成功/失敗を判定");
        ImGui::Spacing();
        ImGui::TextWrapped("アクション: 実際の行動（アニメーション再生など）");

        ImGui::Unindent();
    }

    ImGui::End();
}

void BossAIDebugUI::DrawNodeTree(BehaviorNode* node, const char* nodeName, int depth) {
    if (!node) return;

    // インデント
    for (int i = 0; i < depth; ++i) {
        ImGui::Indent(20.0f);
    }

    // CompositeNodeかどうかチェック
    auto selectorNode = dynamic_cast<SelectorNode*>(node);
    auto sequenceNode = dynamic_cast<SequenceNode*>(node);
    
    if (selectorNode || sequenceNode) {
        // ツリーノードのIDを固定してフレーム間で状態を維持
        ImGui::PushID(nodeName);
        if (ImGui::TreeNode("##node", "%s", nodeName)) {
            if (depth == 0) {
                // ルートノード（Selector）の子ノード
                
                // 近距離パンチ攻撃パターン
                ImGui::PushID("close_punch");
                if (ImGui::TreeNode("##close_punch", "シーケンス: 近距離パンチ攻撃")) {
                    ImGui::BulletText("条件: プレイヤーとの距離 < 3.0");
                    ImGui::BulletText("ランダム判定 (70%%)");
                    ImGui::BulletText("アクション: パンチ攻撃");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                
                // 中距離ジャンプ攻撃パターン
                ImGui::PushID("mid_jump");
                if (ImGui::TreeNode("##mid_jump", "シーケンス: 中距離ジャンプ攻撃")) {
                    ImGui::BulletText("条件: プレイヤーとの距離 3.0~8.0");
                    ImGui::BulletText("ランダム判定 (40%%)");
                    ImGui::BulletText("アクション: ジャンプ攻撃");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                
                // 中距離パンチ攻撃パターン
                ImGui::PushID("mid_punch");
                if (ImGui::TreeNode("##mid_punch", "シーケンス: 中距離パンチ攻撃")) {
                    ImGui::BulletText("条件: プレイヤーとの距離 3.0~8.0");
                    ImGui::BulletText("ランダム判定 (25%%)");
                    ImGui::BulletText("アクション: パンチ攻撃");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                
                // 遠距離接近パターン
                ImGui::PushID("long_walk");
                if (ImGui::TreeNode("##long_walk", "シーケンス: 遠距離接近")) {
                    ImGui::BulletText("条件: プレイヤーとの距離 >= 8.0");
                    ImGui::BulletText("アクション: 歩行");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                
                // デフォルト行動
                ImGui::BulletText("アクション: 待機 (デフォルト)");
            }
            
            ImGui::TreePop();
        }
        ImGui::PopID();
    } else {
        // リーフノード
        ImGui::BulletText("%s", nodeName);
    }

    // インデント解除
    for (int i = 0; i < depth; ++i) {
        ImGui::Unindent(20.0f);
    }
}
