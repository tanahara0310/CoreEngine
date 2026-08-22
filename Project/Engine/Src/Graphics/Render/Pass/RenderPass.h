#pragma once
#include <string>
#include <d3d12.h>

#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderViewType.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    /// @brief View ごとの Graph 実行設定
    struct RenderViewSettings {
        RenderViewType viewType = RenderViewType::GameView;
        std::string viewName; ///< 補助 View の表示名（空 = メイン GameView）。GPU 計測スロット名のプレフィックスに使う
        bool enableSSAO = true;
        bool enableRTShadow = true;
        bool enablePostEffect = true;
        bool enableBackBuffer = true;
        std::string sceneColorTargetName = RenderTargetNames::SceneColor;
    };

    class GraphicsCore;
    class RenderManager;
    class RayTracingSubsystem;
    class SceneManager;
    class ModelManager;
    class PostEffectManager;
    class RenderingTechniqueManager;
    class LightManager;
    class RenderTargetManager;
    class GBufferManager;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    class WaterCausticsRayTracingManager;
    class WaterRefractionRayTracingManager;
    class WaterReflectionRayTracingManager;
    class FFTOceanManager;
    class AtmosphereManager;
    class VolumetricCloudManager;
    class FrameViews;
    class DepthStencilManager;
    class GpuTimestampProfiler;
    /// @brief レンダリングパスのコンテキスト情報
    struct RenderContext {
        GraphicsCore* dxCommon = nullptr;
        /// @brief 今フレームの記録先コマンドリスト
        /// @details パスは必ずこれを使うこと。dxCommon->GetCommandList() を各自で呼ぶと
        ///          「どのコマンドリストへ積むか」の決定がパスの数だけ分散し、
        ///          将来コマンドリストを複数化（アップロード分離・並列記録・async compute）
        ///          した瞬間に全パスが誤ったリストへ積む。供給側を一箇所に固定するための入口。
        ///          代入は EngineSystem::ExecuteRenderPipeline のコンテキスト構築で 1 回だけ。
        ID3D12GraphicsCommandList* cmdList = nullptr;
        RenderManager* renderManager = nullptr;
        RayTracingSubsystem* rayTracingSubsystem = nullptr;
        SceneManager* sceneManager = nullptr;
        PostEffectManager* postEffectManager = nullptr;
        RenderingTechniqueManager* renderingTechniqueManager = nullptr; ///< レンダリング技術管理（SSAO・TAA等）
        LightManager* lightManager = nullptr;
        RenderTargetManager* renderTargetManager = nullptr;
        GBufferManager* gBufferManager = nullptr;  ///< G-Buffer管理（Deferred）
        AccelerationStructureManager* accelerationStructureManager = nullptr; ///< DXR 加速構造管理
        RayTracingShadowManager* rtShadowManager = nullptr; ///< DXR レイトレーシングシャドウ
        WaterCausticsRayTracingManager* rtWaterCausticsManager = nullptr; ///< DXR 水面コースティクス
        WaterRefractionRayTracingManager* rtWaterRefractionManager = nullptr; ///< DXR 水面屈折
        WaterReflectionRayTracingManager* rtWaterReflectionManager = nullptr; ///< DXR 水面反射（鏡像カメラ置き換え）
        FFTOceanManager* fftOceanManager = nullptr; ///< FFT Ocean 波面生成マネージャー
        AtmosphereManager* atmosphereManager = nullptr; ///< 大気散乱管理（LUT生成・太陽情報）
        VolumetricCloudManager* volumetricCloudManager = nullptr; ///< ボリューメトリック雲管理（ノイズ生成・雲合成）
        /// @brief 今フレームの全ビュー（フレーム先頭で確定した不変スナップショット）
        /// @details 描画・カリング・深度復元・RT はすべてここから行列を取ること。
        ///          カメラを直接読みに行くと読み取り時刻で値が変わり、パスごとに
        ///          違う行列を使う事故（SSAO の黒斑）が起きる。
        const FrameViews* frameViews = nullptr;
        DepthStencilManager* depthStencilManager = nullptr; ///< 深度ステンシル管理（バリア遷移・クリアを一元管理）
        FrameBlackboard* frameBlackboard = nullptr; ///< フレーム内共有リソースの論理名管理
        ModelManager* modelManager = nullptr; ///< モデル資産管理（BLAS 遅延ビルド用）
        GpuTimestampProfiler* gpuProfiler = nullptr; ///< パス別 GPU/CPU タイミング計測（nullptr の場合は計測しない）
        const WaterSurfaceData* waterSurfaceState = nullptr; ///< 現在フレームの水面状態（水面不在なら nullptr）
        float fftOceanSimulationTime = 0.0f; ///< FFT Ocean のシミュレーション時刻（水面の表示状態と独立）
        RenderViewSettings viewSettings{}; ///< 現在の View 種別と有効化するパス群設定
        uint32_t currentRTShadowViewId = static_cast<uint32_t>(RenderViewType::GameView); ///< 現在の RT シャドウビュー
        uint64_t frameNumber = 0; ///< フレーム通し番号（View 間で共有。フレーム内 1 回実行パスのガードに使う）
    };

    class RenderGraphBuilder;

    /// @brief レンダーパスの挿入フェーズ
    /// @details パスの登録位置を示す論理フック。同一フェーズ内は priority（小さいほど先）、
    ///          同 priority は登録順。実行順とバリアは最終的に RenderGraph が
    ///          DeclareResources の宣言から導出するため、フェーズは「宣言順の骨格」を決める。
    ///          ユーザー定義パスはエンジンコードを編集せず任意フェーズへ挿入できる。
    enum class RenderPassPhase : uint32_t {
        FrameSetup = 0, ///< LUT 生成・波面計算などフレーム前処理（compute 中心）
        Shadow,         ///< シャドウマップ生成
        GBuffer,        ///< G-Buffer 蓄積
        PreLighting,    ///< SSAO / RTShadow / Caustics などライティング前処理
        Lighting,       ///< Deferred ライティング
        PostLighting,   ///< AerialPerspective などライティング直後の合成
        Sky,            ///< SkyBox / 大気
        Transparent,    ///< 透明オブジェクト
        Water,          ///< 水面（Snapshot 複製 → RT 屈折 → 水面合成）
        PostProcess,    ///< ポストエフェクト
        Overlay,        ///< Sprite / UI / デバッグ描画
        Final,          ///< BackBuffer への最終出力
    };

    /// @brief レンダリングパスの基底クラス
    /// @details パス分離契約: ①DeclareResources で宣言したリソース以外に触れない
    ///          ②残留グローバル状態を変更しない ③FrameBlackboard への登録は Graph 構築前のみ
    ///          ④他パスとの実行順に関する仮定を書かない（順序は宣言した依存からのみ導出される）
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        /// @brief パス名を取得
        /// @return パス名
        virtual const char* GetName() const = 0;

        /// @brief Graph 構築時に自身の Read / Write リソースを宣言する
        /// @details RenderPipeline はこの宣言のみから依存・実行順・バリアを導出する。
        ///          パス追加時にパイプライン側の編集を不要にするための自己記述ポイント。
        /// @param builder Read / Write 宣言を受け取るビルダー
        /// @param context レンダリングコンテキスト（View 設定による宣言分岐に使用）
        virtual void DeclareResources(
            [[maybe_unused]] RenderGraphBuilder& builder,
            [[maybe_unused]] const RenderContext& context) {
        }

        /// @brief Graph 構築前に View 依存の自身の設定を更新する
        /// @details 出力先ターゲット名の切り替えなど、旧 ConfigurePassesForView の移設先。
        /// @param context レンダリングコンテキスト
        virtual void ConfigureForView([[maybe_unused]] const RenderContext& context) {}

        /// @brief この View でパスを Graph へ登録するかを判定する
        /// @param view 現在の View 設定
        /// @return 登録する場合 true
        virtual bool IsEnabledForView([[maybe_unused]] const RenderViewSettings& view) const { return true; }

        /// @brief パスのセットアップ（リソース準備など）
        /// @param context レンダリングコンテキスト
        virtual void Setup([[maybe_unused]] const RenderContext& context) {}

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        virtual void Execute(const RenderContext& context) = 0;

        /// @brief パスのクリーンアップ（リソース解放など）
        /// @param context レンダリングコンテキスト
        virtual void Cleanup([[maybe_unused]] const RenderContext& context) {}

        /// @brief パスが有効かどうか
        /// @return 有効な場合true
        virtual bool IsEnabled() const { return enabled_; }

        /// @brief パスの有効/無効を設定
        /// @param enabled 有効にする場合true
        void SetEnabled(bool enabled) { enabled_ = enabled; }

    protected:
        bool enabled_ = true;
    };
}
