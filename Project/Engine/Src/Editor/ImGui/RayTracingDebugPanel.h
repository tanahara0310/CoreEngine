#pragma once

#ifdef USE_IMGUI

#include <d3d12.h>
#include <string>

namespace CoreEngine
{
    class EngineSystem;
    class GpuTimestampProfiler;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    struct RayTracingDispatchInfo;

    /// @brief レイトレーシング専用デバッグパネル
    /// @details 加速構造の統計・RTシャドウのディスパッチ内訳・中間バッファの可視化・
    ///          RTシャドウ設定の調整をまとめた、"Debug > Ray Tracing" ウィンドウ。
    ///          DebugSubsystem が GameDebugUI::RegisterEngineDebugPanel から登録する。
    ///
    ///          全 RT パスの診断は RayTracingDispatchInfo（共通型）経由でのみ読む。
    ///          Stage 2 で RayTracingShadowManager を共通基盤へ載せ替えても
    ///          このパネルを書き直さずに済むようにするため。
    ///          設計書: Docs/Engine/RayTracing/RayTracing_Refactoring_Review.md
    class RayTracingDebugPanel
    {
    public:
        /// @brief 初期化
        /// @param engine       マネージャ取得元
        /// @param gpuProfiler  パス別 GPU 時間の取得元（DebugSubsystem が所有）
        void Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler);

        /// @brief ImGui コンテンツを描画（RegisterEngineDebugPanel ラムダから呼ぶ）
        void Draw();

    private:
        // ---- 定数 ----
        static constexpr float kThumbLarge = 480.0f;

        // ---- セクション ----
        void DrawAccelerationStructureSection(AccelerationStructureManager* asMgr);
        void DrawShadowSection(RayTracingShadowManager* shadowMgr);
        void DrawShadowSettingsSection(RayTracingShadowManager* shadowMgr);
        void DrawIntermediateBufferSection(RayTracingShadowManager* shadowMgr);
        void DrawWaterSection();
        void DrawEnlargedPopup();

        // ---- ヘルパー ----
        /// @brief RayTracingDispatchInfo を 1 行のテーブル行として描画する
        void DrawDispatchInfoRow(const RayTracingDispatchInfo& info);

        /// @brief パス名から GPU 時間[ms]を引く（見つからなければ -1）
        float FindPassGpuMs(const char* passName) const;

        /// @brief サムネイル 1 枚。クリックで拡大ポップアップ
        void DrawThumbnail(const char* label, const char* tooltip,
            D3D12_GPU_DESCRIPTOR_HANDLE srv, float thumbW, float thumbH);

        static void SectionHeader(const char* label);

        /// @brief バイト数を KB/MB へ整形する
        static std::string FormatBytes(unsigned long long bytes);

        // ---- 状態 ----
        EngineSystem* engine_ = nullptr;
        GpuTimestampProfiler* gpuProfiler_ = nullptr;

        int  selectedLightIndex_ = 0;   ///< 中間バッファ表示の対象ライト
        bool showThumbnails_ = true;

        std::string enlargedTitle_;
        D3D12_GPU_DESCRIPTOR_HANDLE enlargedSrv_ = {};
    };

} // namespace CoreEngine

#endif // USE_IMGUI
