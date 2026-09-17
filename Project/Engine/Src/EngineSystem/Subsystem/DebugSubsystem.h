#pragma once

#ifdef CORE_EDITOR

#include <memory>
#include "Editor/Camera/SceneCameraSection.h"
#include "Editor/ImGui/EditorLayoutSection.h"
#include "Editor/Panel/EditorPanelStateSection.h"
#include <functional>

#include "IEngineSubsystem.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/ImGui/GameDebugUI.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Editor/ImGui/ThreadProfilerUI.h"
#include "Editor/ImGui/KeyConfigUI.h"
#include "Editor/ImGui/EngineStatsWindow.h"
#include "Editor/ImGui/ProfilerPanel.h"
#include "Editor/ImGui/RenderPassDebugPanel.h"
#include "Editor/ImGui/RenderGraphEditorPanel.h"
#include "Editor/ImGui/RayTracingDebugPanel.h"
#include "Graphics/Render/GameOutputWindow.h"
#include "Editor/Environment/AtmosphereEditor.h"
#include "Editor/Environment/VolumetricCloudEditor.h"
#include "Editor/Environment/FogEditor.h"
#include "Editor/Scene/PlayModeController.h"
#include "EngineSystem/Settings/CVarSettingsSection.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/Pass/RenderPipeline.h"

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;
    class Render;
    class GraphicsCore;

    /// @brief デバッグ機能（ImGui / プロファイラ / デバッグUI）の管理サブシステム
    /// @details EngineSystem からデバッグ関連の責務を分離し、肥大化を抑える。
    ///          CORE_EDITOR が定義されたビルドでのみ有効。
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

        /// @brief 計測器を描画の文脈へ渡し、フレーム全体の計測を始める
        void BeginRender(RenderContext& context, const FrameContext& frame) override;

        /// @brief ImGui とゲーム映像専用ウィンドウの描画を積み、フレーム全体の計測を閉じる
        void EndRender(const FrameContext& frame) override;

        /// @brief ゲーム映像専用ウィンドウを提示し、計測結果を読み、外へ出した ImGui ウィンドウを描く
        void AfterPresent() override;

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

        // 再生の前にシーンを控え、停止したら控えから組み直す（gameDebugUI_ より先に破棄される）
        std::unique_ptr<Editor::PlayModeController> playModeController_;

        GpuTimestampProfiler gpuProfiler_;
        std::unique_ptr<ThreadProfilerUI> threadProfilerUI_;
        std::unique_ptr<EngineStatsWindow> engineStatsWindow_;
        std::unique_ptr<ProfilerPanel> profilerPanel_;
        KeyConfigUI keyConfigUI_;
        RenderPassDebugPanel renderPassDebugPanel_;
        RenderGraphEditorPanel renderGraphEditorPanel_;
        RayTracingDebugPanel rayTracingDebugPanel_;

        /// @brief ゲーム映像だけを映す専用 Win32 ウィンドウ（ImGui を経由しない）
        GameOutputWindow gameOutputWindow_;

        // 環境エディタ（大気・雲はエンジン既定機能のため、シーンに依存せずエンジン寿命で保持する）
        // gameDebugUI_ より後に宣言し、デストラクタでの登録解除が UI 解放前に走るようにする
        std::unique_ptr<AtmosphereEditor> atmosphereEditor_;
        std::unique_ptr<VolumetricCloudEditor> cloudEditor_;
        std::unique_ptr<FogEditor> fogEditor_;

        // エディタ設定の自動保存セクション（大気物性・雲。太陽/月ライトはシーン寿命のため
        // EnvironmentFeature 側が別セクションで扱う）。cloudEditor_ を参照するため
        // エディタより後に宣言し、先に破棄されるようにする

        // 全 CVar をまとめて保存するセクション（CVar を増やしてもここへの追記は不要）。
        // 保存先 2 層化により「プロジェクト設定（r./sys. → Config/）」と
        // 「個人の作業状態（d. → Saved/）」の 2 パートに分かれる
        std::unique_ptr<CVarSettingsSection> cvarConfigSection_;
        std::unique_ptr<CVarSettingsSection> cvarStateSection_;

        // 開いているパネルを次の起動へ持ち越す
        std::unique_ptr<Editor::EditorPanelStateSection> panelStateSection_;

        // 画面の配置を次の起動へ持ち越す
        std::unique_ptr<Editor::EditorLayoutSection> layoutSection_;

        // エディタ視点カメラの設定・姿勢を次の起動へ持ち越す
        std::unique_ptr<Editor::SceneCameraSection> sceneCameraSection_;
    };
}

#endif // CORE_EDITOR
