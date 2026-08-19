#pragma once

#ifdef USE_IMGUI

#include <d3d12.h>
#include <imgui.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ImNodesContext;

namespace CoreEngine
{
    class EngineSystem;
    class GpuTimestampProfiler;
    class RenderPass;
    class RenderPipeline;
    struct RenderGraphSnapshot;

    /// @brief RenderGraph ノードエディタ（imnodes・"Tools > Render Graph"）
    /// @details View ごとのスナップショットを、パス＝ノード / 論理リソース＝ピンとして描画する。
    /// @note ノード座標は依存の最長経路から毎回自動算出するので、手で並べる必要はない。
    class RenderGraphEditorPanel
    {
    public:
        /// @brief 初期化（ImGui コンテキスト生成後に呼ぶ）
        /// @param engine エンジンシステム（RenderPipeline 取得用）
        /// @param profiler パス別 GPU/CPU 時間の取得元
        void Initialize(EngineSystem* engine, GpuTimestampProfiler* profiler);

        /// @brief 終了処理（ImGui 解放より前に呼ぶこと）
        void Finalize();

        /// @brief フレーム先頭で呼び、パネルが閉じられていればスナップショット複製を止める
        /// @details Draw() は「ウィンドウが開いていて畳まれていない」ときしか呼ばれないため、
        ///          止める側の判断はフレーム先頭のこの関数が担う。
        void SyncCaptureState();

        /// @brief ImGui コンテンツを描画（RegisterEnginePanel ラムダから呼ぶ）
        void Draw();

    private:
        /// @brief ノードの色分けモード
        enum class NodeColorMode : int {
            Category = 0, ///< GpuTimingCategory（Water / Sky / PostProcess 等の塊が見える）
            GpuTime,      ///< GPU 時間のヒートマップ（重いパスが赤くなる）
            Count
        };

        /// @brief 描画済みリンク 1 本の由来（ホバー時の説明に使う）
        struct LinkInfo {
            uint32_t fromPass = 0;
            uint32_t toPass = 0;
            std::string resourceName;
            const char* kindLabel = "";
        };

        // ---- 描画 ----
        void DrawToolbar(const RenderGraphSnapshot* snapshot);
        void DrawGraph(const RenderGraphSnapshot& snapshot);
        void DrawNode(const RenderGraphSnapshot& snapshot, uint32_t passIndex);
        void DrawDetailPanel(const RenderGraphSnapshot& snapshot);
        void DrawNodeContextMenu(const RenderGraphSnapshot& snapshot);
        void DrawEnlargedPreview();

        // ---- レイアウト ----
        /// @brief 依存の最長経路をレイヤとして横方向へ、バリセンター法で縦方向へ配置する
        void ComputeLayout(const RenderGraphSnapshot& snapshot);

        /// @brief 構成が変わったか判定するためのトポロジハッシュ
        static size_t ComputeTopologyHash(const RenderGraphSnapshot& snapshot);

        // ---- 補助 ----
        /// @brief 選択パスから辿れる上流／下流のパス集合を求める
        void RebuildHighlightSets(const RenderGraphSnapshot& snapshot, uint32_t rootPass);

        /// @brief ハイライト対象（選択パスの上流または下流）かを返す
        /// @details 未選択・ハイライト無効・マスク未構築のときは全ノードを対象扱いにする。
        bool IsHighlighted(uint32_t passIndex) const
        {
            if (!highlightDependencies_ || highlightRootPass_ < 0) {
                return true;
            }
            if (passIndex >= upstreamMask_.size() || passIndex >= downstreamMask_.size()) {
                return true;
            }
            return upstreamMask_[passIndex] != 0 || downstreamMask_[passIndex] != 0;
        }

        /// @brief パス名から GPU / CPU 時間を引く（見つからなければ 0）
        void LookupTiming(const std::string& timingLabel, float& outGpuMs, float& outCpuMs) const;

        /// @brief 今フレームのパス別計測値をまとめて引き直す
        /// @details 計測スロットは線形探索なので、描画のたびに引くとパス数の二乗になる。
        ///          フレーム先頭で 1 回だけ引いてインデックス参照に落とす。
        void RefreshTimings(const RenderGraphSnapshot& snapshot);

        float PassGpuMs(uint32_t passIndex) const
        {
            return passIndex < passGpuMs_.size() ? passGpuMs_[passIndex] : 0.0f;
        }

        float PassCpuMs(uint32_t passIndex) const
        {
            return passIndex < passCpuMs_.size() ? passCpuMs_[passIndex] : 0.0f;
        }

        /// @brief 一括操作の前に現在の有効／無効状態を退避する
        void SaveEnableStates(const RenderGraphSnapshot& snapshot);

        /// @brief 退避しておいた有効／無効状態へ戻す
        void RestoreEnableStates();

        RenderPipeline* GetPipeline() const;

        // ---- 定数 ----
        static constexpr int kAttributeStride = 128; ///< 1 パスあたりに確保するアトリビュート ID 幅
        static constexpr int kMaxPinsPerSide = 60;   ///< 片側に出すピンの上限（超過分は集約表示）
        static constexpr float kNodeContentWidth = 190.0f;

        // ---- 状態 ----
        EngineSystem* engine_ = nullptr;
        GpuTimestampProfiler* profiler_ = nullptr;
        ImNodesContext* nodesContext_ = nullptr;

        int selectedViewIndex_ = 0;
        int selectedPassIndex_ = -1;      ///< 詳細パネルに出しているパス（-1 = 未選択）
        int contextMenuPassIndex_ = -1;   ///< 右クリックメニューの対象パス
        NodeColorMode colorMode_ = NodeColorMode::GpuTime;

        bool paused_ = false;
        bool showMiniMap_ = true;
        bool highlightDependencies_ = true;
        bool drawnRecently_ = false;
        char nameFilter_[64] = {};

        // レイアウト
        std::vector<ImVec2> nodePositions_;
        std::vector<int> nodeDepths_;
        size_t layoutTopologyHash_ = 0;
        int layoutViewIndex_ = -1;
        bool layoutRequested_ = true;
        /// @brief 次の描画で自動配置座標をノードへ書き戻すか
        /// @details 毎フレーム書き戻すとドラッグで動かしたノードが即座に戻って掴めなくなるため、
        ///          再レイアウトした直後の 1 フレームだけ適用する。
        bool applyLayoutPositions_ = false;
        float columnSpacing_ = 300.0f;
        float rowSpacing_ = 130.0f;

        // ハイライト（選択パスの上流・下流）
        std::vector<uint8_t> upstreamMask_;
        std::vector<uint8_t> downstreamMask_;
        int highlightRootPass_ = -1;

        // リンク
        std::vector<LinkInfo> linkInfos_;

        // 今フレームのパス別計測値（RefreshTimings で更新）
        std::vector<float> passGpuMs_;
        std::vector<float> passCpuMs_;
        float maxPassGpuMs_ = 0.0f;
        float totalPassGpuMs_ = 0.0f;

        // 一括操作の退避（Solo / Disable downstream からの復帰用）
        std::unordered_map<RenderPass*, bool> savedEnableStates_;

        // 拡大プレビュー
        std::string enlargedTitle_;
        D3D12_GPU_DESCRIPTOR_HANDLE enlargedSrv_{};
    };
}

#endif // USE_IMGUI
