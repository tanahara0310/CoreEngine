#pragma once

#ifdef USE_IMGUI

#include <memory>
#include <functional>

#include "IEngineSubsystem.h"
#include "Utility/Debug/ImGui/ImGuiManager.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Graphics/Common/GpuTimestampProfiler.h"
#include "Utility/Debug/ImGui/ThreadProfilerUI.h"
#include "Utility/Debug/ImGui/KeyConfigUI.h"
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

        /// @brief SceneView の描画（GBufferPass → DeferredLighting → GeometryPass → RT コピー）
        /// @param context          レンダリングコンテキスト（currentRTShadowViewId を書き換えるため非 const ref）
        /// @param renderPipeline   現在のレンダーパイプライン
        /// @param render           Render コンポーネント（RenderTarget 取得用）
        /// @param cmdList          コマンドリスト（SceneView RT への blit 用）
        /// @param inOutPreviousOutput パスチェーンの出力（呼び出し前後で保存・復元される）
        /// @param executePass      パス実行ラムダ（EngineSystem::ExecuteRenderPipeline のローカルラムダ）
        /// @param gameRenderCallback ゲームビュー用の GeometryPass コールバック（SceneView 後に復元）
        void RenderSceneView(
            RenderContext& context,
            RenderPipeline* renderPipeline,
            Render* render,
            ID3D12GraphicsCommandList* cmdList,
            PassOutput& inOutPreviousOutput,
            const std::function<void(RenderPass*)>& executePass,
            const std::function<void()>& gameRenderCallback);

        // ──────────────────────────────────────────────────────────
        // アクセサ
        // ──────────────────────────────────────────────────────────

        ImGuiManager* GetImGuiManager() { return imGui_.get(); }
        GameDebugUI* GetGameDebugUI() { return gameDebugUI_.get(); }
        DockingUI* GetDockingUI() { return imGui_ ? imGui_->GetDockingUI() : nullptr; }
        ConsoleUI* GetConsole() { return gameDebugUI_ ? gameDebugUI_->GetConsole() : nullptr; }

        GpuTimestampProfiler& GetGpuProfiler() { return gpuProfiler_; }

        bool IsSceneViewPostEffectEnabled() const { return sceneViewEnablePostEffect_; }

    private:
        EngineSystem* engine_ = nullptr;

        std::unique_ptr<ImGuiManager> imGui_;
        std::unique_ptr<GameDebugUI> gameDebugUI_;
        GpuTimestampProfiler gpuProfiler_;
        std::unique_ptr<ThreadProfilerUI> threadProfilerUI_;
        KeyConfigUI keyConfigUI_;

        // SceneView 設定（configから読み込み）
        bool sceneViewEnablePostEffect_ = false;
    };
}

#endif // USE_IMGUI
