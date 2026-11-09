#pragma once

#ifdef CORE_EDITOR

#include <cstdint>
#include <string>
#include <vector>

#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Debug/RTDebugSystem.h"
#include "Graphics/Render/Line/ILineSource.h"

namespace CoreEngine
{
    class EngineSystem;
    class GpuTimestampProfiler;
    class LineRendererPipeline;
    class RTScene;
    class AccelerationStructureManager;

    namespace Editor {
        struct GameViewportOverlayArgs;
    }

    /// @brief レイトレーシングのデバッグ窓（"Debug > Ray Tracing"）
    /// @details 6 つのタブ（概要・中間結果・見え方・レイの追跡・加速構造・チェック）と、全部のタブに出る帯を持つ。
    ///          効果・中間結果・チェックは RTDebugRegistry の申告を並べるだけで、効果の種類を知らない。
    ///          窓を閉じると、切り分けのスイッチ（凍結・乱数の固定・止めた効果）は元へ戻る。
    class RayTracingDebugPanel : public ILineSource
    {
    public:
        /// @brief 初期化
        /// @param engine       マネージャ取得元
        /// @param gpuProfiler  パス別 GPU 時間の取得元（DebugSubsystem が所有）
        void Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler);

        /// @brief 線の登録とゲーム画面へ重ねる口を外す
        void Finalize();

        /// @brief フレームの頭に呼ぶ（前のフレームに窓を描いていなければ切り分けのスイッチを戻す）
        void BeginFrame();

        /// @brief ImGui コンテンツを描画（EditorPanelRegistry のラムダから呼ぶ）
        void Draw();

        /// @brief 追跡したレイと選んだ物の箱を線で描く
        void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) override;

    private:
        /// @brief 中間結果を出す先
        enum class OutputTarget : int {
            Window = 0,   ///< この窓の中だけ
            Fullscreen,   ///< ゲーム画面いっぱい
            Split,        ///< ゲーム画面の左半分
        };

        /// @brief 今見ている画像の出どころ
        enum class ViewSource : int {
            None = 0,
            Channel,      ///< 中間結果タブで選んだもの
            Scene,        ///< 見え方タブ
        };

        /// @brief 中間結果の見方（中間結果タブと見え方タブで別々に持つ）
        struct ViewSettings {
            RTDebugViewMode mode = RTDebugViewMode::Mono;
            uint32_t component = 0;
            uint32_t componentMask = 0x7;
            float rangeMin = 0.0f;
            float rangeMax = 1.0f;
            bool alphaMarksMiss = false;
        };

        /// @brief チェックの結果 1 件（記録と比べる用に名前を付けて持つ）
        struct CheckLine {
            std::string check;
            RTDebugSeverity severity = RTDebugSeverity::Ok;
            std::string message;
        };

        // ---- 共通 ----
        RTDebugQuery BuildQuery() const;
        RTDebugSystem* GetDebugSystem() const;
        void RunChecks(const RTDebugQuery& query);
        void DrawCommonBar(const RTDebugQuery& query);
        void ResetSwitches();
        void EnsureLineSource();

        // ---- タブ ----
        void DrawOverviewTab(const RTDebugQuery& query);
        void DrawChannelsTab(RTDebugSystem& debug);
        void DrawSceneTab(const RTDebugQuery& query, RTDebugSystem& debug);
        void DrawTraceTab(const RTDebugQuery& query, RTDebugSystem& debug);
        void DrawAccelerationTab(const RTDebugQuery& query);
        void DrawChecksTab(RTDebugSystem& debug);

        // ---- 中間結果の表示 ----
        /// @brief 見方を中間結果の既定へ戻す
        void ApplyChannelDefaults(const RTDebugChannelDesc& channel, ViewSettings& settings) const;
        /// @brief 見方の欄（色の付け方・成分・範囲）
        void DrawViewSettings(const RTDebugChannelDesc& channel, ViewSettings& settings, const RTDebugImageStats& stats);
        /// @brief 出す先の欄
        void DrawOutputTarget(bool screenSpace);
        /// @brief 色を付けた画像を窓に出し、上にマウスがあれば画素を返す
        bool DrawPreview(RTDebugSystem& debug, int channel, uint32_t& outX, uint32_t& outY);
        /// @brief 画素の値の行
        void DrawProbe(const RTDebugChannelDesc& channel, const RTDebugImageStats& stats);
        /// @brief 頼みを作る
        RTDebugViewRequest MakeViewRequest(int channel, uint32_t variant, const ViewSettings& settings) const;

        /// @brief ゲーム画面へ重ねる（クリックを使ったら true）
        bool DrawGameViewportOverlay(const Editor::GameViewportOverlayArgs& args);

        // ---- 状態 ----
        EngineSystem* engine_ = nullptr;
        GpuTimestampProfiler* gpuProfiler_ = nullptr;
        uint32_t overlayId_ = 0;
        LineRendererPipeline* linePipeline_ = nullptr;
        bool drawnThisFrame_ = false;
        bool windowOpen_ = false;
        int selectTab_ = -1;            ///< 次のフレームに開くタブ（-1 = 切り替えない）

        // 中間結果タブ
        int selectedChannel_ = -1;
        uint32_t selectedVariant_ = 0;
        ViewSettings channelView_{};
        OutputTarget outputTarget_ = OutputTarget::Window;
        ViewSource viewSource_ = ViewSource::None;

        // 画素の値を読む位置（中間結果の画素の座標）
        bool probeEnabled_ = false;
        uint32_t probeX_ = 0;
        uint32_t probeY_ = 0;

        // 見え方タブ
        RTDebugSceneRequest sceneRequest_{};
        ViewSettings sceneView_{};
        bool sceneViewInitialized_ = false;

        // レイの追跡タブ
        int traceX_ = 0;
        int traceY_ = 0;
        bool tracePicking_ = false;     ///< 次にゲーム画面をクリックした画素を追う
        bool traceFollowMouse_ = false; ///< ゲーム画面のマウスの下を追い続ける
        bool traceHold_ = false;        ///< 追い直さず、今の結果を残す
        bool traceActive_ = false;      ///< 追跡の線と印を出すか（追跡のタブを見ているか、止めて残している）
        bool drawTraceLines_ = true;
        int traceDispatchFilter_ = -1;  ///< 表と線に出す効果のディスパッチ（-1 = 全部）

        // 加速構造タブ
        char accelFilter_[64] = {};
        int accelSource_ = -1;          ///< -1 = 全部
        int accelShape_ = 0;            ///< 0 = 全部・1 = 静止形・2 = 毎フレーム作る
        uint32_t accelMask_ = 0xFF;
        int selectedRow_ = -1;
        bool drawSelectedBox_ = true;

        // チェックタブ
        bool scanChannels_ = false;
        std::vector<CheckLine> checkLines_;
        std::vector<CheckLine> recordedLines_;
        bool hasRecord_ = false;
        uint32_t warningCount_ = 0;
        uint32_t errorCount_ = 0;
    };

} // namespace CoreEngine

#endif // CORE_EDITOR
