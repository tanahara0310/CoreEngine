#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief GPU タイムスタンプ計測スロット
    /// @details Total / ImGuiDraw は RenderGraph 外で計測する固定スロット。
    ///          RenderGraph に登録された各パスは実行時に名前から動的スロットを取得する
    ///          （GpuTimestampProfiler::GetOrCreateNamedSlot）ため、新規パス追加時に
    ///          この enum を編集する必要はない。
    enum class GpuTimestampSlot : uint32_t
    {
        ImGuiDraw = 0,
        Total,
        Count
    };

    /// @brief タイミング表示のカテゴリ（RenderPassPhase とおおむね対応）
    /// @details UI 側のグルーピング表示に使う。パスの実行フェーズから機械的に決まるため、
    ///          UI 側でパス名をハードコードした振り分けテーブルを持つ必要がない。
    enum class GpuTimingCategory : uint8_t
    {
        Setup = 0,      ///< FrameSetup（LUT生成・波面計算など）
        Shadow,         ///< シャドウマップ生成
        GBuffer,        ///< G-Buffer 蓄積
        PreLighting,    ///< SSAO / RTShadow / Caustics などライティング前処理
        Lighting,       ///< Deferred ライティング
        PostLighting,   ///< AerialPerspective などライティング直後の合成
        Sky,            ///< SkyBox / 大気 / ボリューメトリック雲 / ゴッドレイ
        Transparent,    ///< 透明オブジェクト
        Water,          ///< 水面
        PostProcess,    ///< ポストエフェクト
        Overlay,        ///< Sprite / UI / デバッグ描画
        Final,          ///< BackBuffer への最終出力
        Editor,         ///< ImGui 描画など Graph 外の計測
        Frame,          ///< フレーム合計（Total）
    };

    /// @brief 1スロット分の CPU / GPU 計測結果
    struct GpuTimingResult
    {
        const char* name = "";
        float cpuMs = 0.0f;
        float gpuMs = 0.0f;
        GpuTimingCategory category = GpuTimingCategory::Setup;
    };

    /// @brief DirectX12 タイムスタンプクエリを用いた GPU プロファイラー
    ///
    /// 使い方:
    ///   1. Initialize(device)
    ///   2. 毎フレーム NewFrame(currentBackBufferIndex)
    ///   3. コマンドリスト記録中に BeginGpuTimestamp / EndGpuTimestamp
    ///      （RenderGraph に登録されたパスは RenderGraph::Execute が自動で計測する）
    ///   4. Close() 前に ResolveAll(cmdList, currentBackBufferIndex)
    ///   5. FinalizeFrame() 完了後に ReadResults(commandQueue, nextBackBufferIndex)
    class GpuTimestampProfiler
    {
    public:
        static constexpr uint32_t kFixedSlotCount = static_cast<uint32_t>(GpuTimestampSlot::Count);
        static constexpr uint32_t kMaxDynamicSlots = 48; ///< RenderGraph パス + PostEffect の名前付きスロット上限
        static constexpr uint32_t kSlotCount = kFixedSlotCount + kMaxDynamicSlots;
        static constexpr uint32_t kFrameCount = 2;
        static constexpr uint32_t kQueriesPerSlot = 2; // begin + end
        static constexpr uint32_t kQueriesPerFrame = kSlotCount * kQueriesPerSlot;

        /// @brief UI がグルーピング表示に使うカテゴリの表示順（Frame は Total 専用行として別扱い）
        static constexpr std::array<GpuTimingCategory, 13> kCategoryDisplayOrder = {
            GpuTimingCategory::Setup,
            GpuTimingCategory::Shadow,
            GpuTimingCategory::GBuffer,
            GpuTimingCategory::PreLighting,
            GpuTimingCategory::Lighting,
            GpuTimingCategory::PostLighting,
            GpuTimingCategory::Sky,
            GpuTimingCategory::Transparent,
            GpuTimingCategory::Water,
            GpuTimingCategory::PostProcess,
            GpuTimingCategory::Overlay,
            GpuTimingCategory::Final,
            GpuTimingCategory::Editor,
        };

        /// @brief 初期化
        void Initialize(ID3D12Device* device);

        /// @brief 終了処理
        void Finalize();

        /// @brief フレーム開始時に呼ぶ (frameIndex を更新し CPU 計測をリセット)
        void NewFrame(uint32_t frameIndex);

        /// @brief パス名から計測スロットを取得（未登録なら新規登録）
        /// @param name パス名（RenderGraphPass::name など。表示名としてそのまま使われる）
        /// @param category UI グルーピング用カテゴリ（新規登録時のみ反映）
        /// @return スロットインデックス（上限超過時は UINT32_MAX）
        uint32_t GetOrCreateNamedSlot(const std::string& name, GpuTimingCategory category = GpuTimingCategory::Setup);

        // ── GPU タイムスタンプ ──────────────────────────────────

        /// @brief スロット開始タイムスタンプをコマンドリストに積む
        void BeginGpuTimestamp(uint32_t slotIndex, ID3D12GraphicsCommandList* cmdList);
        void BeginGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList) {
            BeginGpuTimestamp(static_cast<uint32_t>(slot), cmdList);
        }

        /// @brief スロット終了タイムスタンプをコマンドリストに積む
        void EndGpuTimestamp(uint32_t slotIndex, ID3D12GraphicsCommandList* cmdList);
        void EndGpuTimestamp(GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList) {
            EndGpuTimestamp(static_cast<uint32_t>(slot), cmdList);
        }

        /// @brief 全クエリを readback バッファへ解決 (cmdList->Close() 前に必須)
        void ResolveAll(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);

        /// @brief readback バッファから GPU 結果を読み出し lastResults_ に格納
        /// @note  WaitForFrame(readFrameIndex) 完了後に呼ぶこと
        void ReadResults(ID3D12CommandQueue* commandQueue, uint32_t readFrameIndex);

        // ── CPU タイムスタンプ ──────────────────────────────────

        /// @brief スロットの CPU 計測を開始
        void BeginCpuTimestamp(uint32_t slotIndex);
        void BeginCpuTimestamp(GpuTimestampSlot slot) { BeginCpuTimestamp(static_cast<uint32_t>(slot)); }

        /// @brief スロットの CPU 計測を終了
        void EndCpuTimestamp(uint32_t slotIndex);
        void EndCpuTimestamp(GpuTimestampSlot slot) { EndCpuTimestamp(static_cast<uint32_t>(slot)); }

        // ── 結果取得 ──────────────────────────────────────────

        /// @brief 直近フレームの計測結果を返す (1フレーム遅延あり)
        const std::array<GpuTimingResult, kSlotCount>& GetResults() const { return lastResults_; }

        bool IsInitialized() const { return initialized_; }

        /// @brief スロット番号に対応する表示名を返す
        const char* GetSlotName(uint32_t slotIndex) const noexcept
        {
            switch (static_cast<GpuTimestampSlot>(slotIndex))
            {
            case GpuTimestampSlot::ImGuiDraw: return "ImGui Draw";
            case GpuTimestampSlot::Total:     return "Frame Total";
            default: break;
            }
            const uint32_t dynIndex = slotIndex - kFixedSlotCount;
            if (dynIndex < dynamicSlotCount_) {
                return dynamicNames_[dynIndex].c_str();
            }
            return "";
        }

        /// @brief カテゴリの表示ラベルを返す
        static constexpr const char* GetCategoryLabel(GpuTimingCategory category) noexcept
        {
            switch (category)
            {
            case GpuTimingCategory::Setup:        return "Setup";
            case GpuTimingCategory::Shadow:       return "Shadow";
            case GpuTimingCategory::GBuffer:      return "G-Buffer";
            case GpuTimingCategory::PreLighting:  return "Pre-Lighting";
            case GpuTimingCategory::Lighting:     return "Lighting";
            case GpuTimingCategory::PostLighting: return "Post-Lighting";
            case GpuTimingCategory::Sky:          return "Sky / Atmosphere";
            case GpuTimingCategory::Transparent:  return "Transparent";
            case GpuTimingCategory::Water:        return "Water";
            case GpuTimingCategory::PostProcess:  return "Post Process";
            case GpuTimingCategory::Overlay:      return "Overlay";
            case GpuTimingCategory::Final:        return "Final";
            case GpuTimingCategory::Editor:       return "Editor";
            case GpuTimingCategory::Frame:        return "Frame Total";
            default:                              return "Other";
            }
        }

        /// @brief RAII スコープで CPU/GPU タイムスタンプを計測するガード
        class ProfileScope
        {
        public:
            ProfileScope(GpuTimestampProfiler& profiler, GpuTimestampSlot slot, ID3D12GraphicsCommandList* cmdList)
                : profiler_(profiler), slot_(static_cast<uint32_t>(slot)), cmdList_(cmdList)
            {
                profiler_.BeginCpuTimestamp(slot_);
                profiler_.BeginGpuTimestamp(slot_, cmdList_);
            }
            ~ProfileScope()
            {
                profiler_.EndGpuTimestamp(slot_, cmdList_);
                profiler_.EndCpuTimestamp(slot_);
            }
            ProfileScope(const ProfileScope&) = delete;
            ProfileScope& operator=(const ProfileScope&) = delete;
        private:
            GpuTimestampProfiler& profiler_;
            uint32_t                    slot_;
            ID3D12GraphicsCommandList* cmdList_;
        };

    private:
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
        Microsoft::WRL::ComPtr<ID3D12Resource>  readbackBuffers_[kFrameCount];

        uint32_t currentFrameIndex_ = 0;

        // フレーム内で実際に BeginGpuTimestamp されたスロットを追跡するフラグ
        std::array<std::array<bool, kSlotCount>, kFrameCount> activatedSlots_ = {};

        // CPU タイム (ダブルバッファ: [frameIndex][slotIndex])
        std::chrono::high_resolution_clock::time_point cpuBegin_[kFrameCount][kSlotCount] = {};
        float cpuTimesMs_[kFrameCount][kSlotCount] = {};

        std::array<GpuTimingResult, kSlotCount> lastResults_ = {};
        bool initialized_ = false;

        // 名前 → 動的スロットインデックスの登録テーブル
        std::unordered_map<std::string, uint32_t> namedSlotIndex_;
        std::array<std::string, kMaxDynamicSlots> dynamicNames_;
        std::array<GpuTimingCategory, kMaxDynamicSlots> dynamicCategories_ = {};
        uint32_t dynamicSlotCount_ = 0;
    };

    /// @brief カテゴリごとにグルーピングされたタイミング表示行
    struct GpuTimingGroup
    {
        GpuTimingCategory category;
        std::vector<uint32_t> slotIndices; ///< results 内の非ゼロ・当該カテゴリのインデックス
    };

    /// @brief GetResults() の結果をカテゴリ別（表示順）にグルーピングする
    /// @details DockingUI / EngineStatsWindow が同じ分類・順序でタイミング表を描画するための共通ヘルパー。
    ///          Frame（Total）は専用行として別扱いするためここには含めない。
    inline std::vector<GpuTimingGroup> BuildGpuTimingGroups(
        const std::array<GpuTimingResult, GpuTimestampProfiler::kSlotCount>& results)
    {
        std::vector<GpuTimingGroup> groups;
        for (GpuTimingCategory category : GpuTimestampProfiler::kCategoryDisplayOrder)
        {
            GpuTimingGroup group{ category, {} };
            for (uint32_t i = 0; i < results.size(); ++i)
            {
                const GpuTimingResult& r = results[i];
                if (r.category != category) continue;
                if (r.gpuMs < 0.001f && r.cpuMs < 0.001f) continue;
                group.slotIndices.push_back(i);
            }
            if (!group.slotIndices.empty()) {
                groups.push_back(std::move(group));
            }
        }
        return groups;
    }
}
