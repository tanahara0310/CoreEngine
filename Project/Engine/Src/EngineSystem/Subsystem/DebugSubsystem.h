#pragma once

#ifdef USE_IMGUI

#include <memory>
#include <functional>

#include "IEngineSubsystem.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Graphics/Common/GpuTimestampProfiler.h"
#include "Editor/ImGui/ThreadProfilerUI.h"
#include "Editor/ImGui/KeyConfigUI.h"
#include "Editor/ImGui/EngineStatsWindow.h"
#include "Editor/ImGui/RenderPassDebugPanel.h"
#include "Editor/Environment/AtmosphereEditor.h"
#include "Editor/Environment/VolumetricCloudEditor.h"
#include "Editor/Environment/AtmosphereSettingsSection.h"
#include "Editor/Environment/VolumetricCloudSettingsSection.h"
#include "Graphics/PostEffect/Effect/PostEffectSettingsSection.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueSettingsSection.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/Pass/RenderPipeline.h"

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;
    class Render;
    class DirectXCommon;

    /// @brief デバッグ機能（ImGui / プロファイラ / デバッグUI）の管理サブシステム
    /// @details EngineSystem からデバッグ関連の責務を分離し、肥大化を抑える。
    ///          USE_IMGUI が定義されたビルドでのみ有効。
    class DebugSubsystem : public IEngineSubsystem
    {
    public:
        DebugSubsystem();
        ~DebugSubsystem();

        DebugSubsystem(const DebugSubsystem&) = delete;
        DebugSubsystem& operator=(const DebugSubsystem&) = delete;

        const char* GetName() const noexcept override { return "DebugSubsystem"; }

        /// @brief 初期化（EngineSystem のグラフィックス系コンポーネント生成後に呼ぶ）
        void Initialize(EngineSystem* engine, const EngineConfig& config) override;

        /// @brief 終了処理（ImGui 解放やコンソールコールバック解除）
        void Finalize() override;

        /// @brief フレーム開始処理（ImGui::Begin、メニューバー、デバッグパネル更新など）
        void BeginFrame() override;

        /// @brief フレーム終了処理（ImGui::End）
        void EndFrame() override;

        /// @brief レンダーパイプライン開始時のプロファイル計測を開始する
        /// @param cmdList      コマンドリスト
        /// @param frameIndex   現在のバックバッファインデックス
        void BeginRenderPipeline(ID3D12GraphicsCommandList* cmdList, UINT frameIndex);

        /// @brief ImGui の描画コマンドを積む（プロファイルスコープ付き）
        /// @param cmdList コマンドリスト
        void DrawImGuiWithProfiling(ID3D12GraphicsCommandList* cmdList);

        /// @brief レンダーパイプライン終了時のプロファイル計測を終了・解決する
        /// @param cmdList      コマンドリスト
        /// @param frameIndex   現在のバックバッファインデックス
        void EndRenderPipeline(ID3D12GraphicsCommandList* cmdList, UINT frameIndex);

        /// @brief FinalizeFrame 完了後にGPU計測結果を読み取り、DockingUI へ反映する
        /// @param dx DirectXCommon（コマンドキュー・フレームインデックス取得用）
        void PostFinalizeFrame(DirectXCommon* dx);

        // ──────────────────────────────────────────────────────────
        // アクセサ
        // ──────────────────────────────────────────────────────────

        ImGuiManager* GetImGuiManager() { return imGui_.get(); }
        GameDebugUI* GetGameDebugUI() { return gameDebugUI_.get(); }
        DockingUI* GetDockingUI() { return imGui_ ? imGui_->GetDockingUI() : nullptr; }
        ConsoleUI* GetConsole() { return gameDebugUI_ ? gameDebugUI_->GetConsole() : nullptr; }

        GpuTimestampProfiler& GetGpuProfiler() { return gpuProfiler_; }

    private:
        EngineSystem* engine_ = nullptr;

        std::unique_ptr<ImGuiManager> imGui_;
        std::unique_ptr<GameDebugUI> gameDebugUI_;
        GpuTimestampProfiler gpuProfiler_;
        std::unique_ptr<ThreadProfilerUI> threadProfilerUI_;
        std::unique_ptr<EngineStatsWindow> engineStatsWindow_;
        KeyConfigUI keyConfigUI_;
        RenderPassDebugPanel renderPassDebugPanel_;

        // 環境エディタ（大気・雲はエンジン既定機能のため、シーンに依存せずエンジン寿命で保持する）
        // gameDebugUI_ より後に宣言し、デストラクタでの登録解除が UI 解放前に走るようにする
        std::unique_ptr<AtmosphereEditor> atmosphereEditor_;
        std::unique_ptr<VolumetricCloudEditor> cloudEditor_;

        // エディタ設定の自動保存セクション（大気物性・雲。太陽/月ライトはシーン寿命のため
        // EnvironmentFeature 側が別セクションで扱う）。cloudEditor_ を参照するため
        // エディタより後に宣言し、先に破棄されるようにする
        std::unique_ptr<AtmosphereSettingsSection> atmosphereSettingsSection_;
        std::unique_ptr<VolumetricCloudSettingsSection> cloudSettingsSection_;
        std::unique_ptr<PostEffectSettingsSection> postEffectSettingsSection_;
        std::unique_ptr<RenderingTechniqueSettingsSection> renderingTechniqueSettingsSection_;
    };
}

#endif // USE_IMGUI
