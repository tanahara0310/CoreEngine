#pragma once
#include <string>
#include <d3d12.h>

namespace CoreEngine
{
    class DirectXCommon;
    class RenderManager;
    class PostEffectManager;
    class RenderingTechniqueManager;
    class LightManager;
    class RenderTargetManager;
    class GBufferManager;
    class ShadowMapManager;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    class CameraManager;
    class DepthStencilManager;

    /// @brief レンダリングパスのコンテキスト情報
    struct RenderContext {
        DirectXCommon* dxCommon = nullptr;
        RenderManager* renderManager = nullptr;
        PostEffectManager* postEffectManager = nullptr;
        RenderingTechniqueManager* renderingTechniqueManager = nullptr; ///< レンダリング技術管理（SSAO・TAA等）
        LightManager* lightManager = nullptr;
        ShadowMapManager* shadowMapManager = nullptr;  ///< シャドウマップ管理（LVP記列・ SRV 取得用）
        RenderTargetManager* renderTargetManager = nullptr;
        GBufferManager* gBufferManager = nullptr;  ///< G-Buffer管理（Deferred）
        AccelerationStructureManager* accelerationStructureManager = nullptr; ///< DXR 加速構造管理
        RayTracingShadowManager* rtShadowManager = nullptr; ///< DXR レイトレーシングシャドウ
        CameraManager* cameraManager = nullptr; ///< カメラ管理（SSAO等でビュー/プロジェクション行列取得用）
        DepthStencilManager* depthStencilManager = nullptr; ///< 深度ステンシル管理（バリア遷移・クリアを一元管理）
        uint32_t currentRTShadowViewId = 1; ///< 現在の RT シャドウビュー (0=SceneView, 1=GameView)
    };

    /// @brief パス間のデータ受け渡し用構造体
    struct PassOutput {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};  ///< 出力テクスチャのSRVハンドル
        ID3D12Resource* resource = nullptr;        ///< 出力リソース（オプション）
        bool isValid = false;                      ///< 有効なデータかどうか

        /// @brief 出力をリセット
        void Reset() {
            srvHandle = {};
            resource = nullptr;
            isValid = false;
        }
    };

    /// @brief レンダリングパスの基底クラス
    ///
    /// @details
    ///  ## 役割
    ///  RenderPass は「フレーム内の 1 ステージ」を表現する高レベル
    ///  パイプラインノード。RenderTarget の切り替え、リソースバリア、
    ///  必要な IRenderer の呼び分けを担当する。
    ///
    ///  ## IRenderer との違い
    ///  - RenderPass : 「いつ・どこに描くか」（フレーム構成の単位）
    ///  - IRenderer  : 「何を描くか」（PSO/DrawCall 発行の単位）
    ///
    ///  RenderPass は内部で 0 個以上の IRenderer を利用してパスを構成する。
    ///  両者は疎結合であり、RenderPass を増やしても IRenderer の実装変更を
    ///  必要としない。
    ///
    ///  ## ライフサイクル
    ///   1. Setup(context)   - 一度だけリソース準備（任意）
    ///   2. Execute(context) - 毎フレーム実行
    ///   3. Cleanup(context) - 終了時にリソース解放（任意）
    ///
    ///  ## パス間連携
    ///  SetInput / GetOutput を介して前段パスの出力テクスチャ等を
    ///  次段に受け渡す（例: GeometryPass → PostEffectPass）。
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        /// @brief パス名を取得
        /// @return パス名
        virtual const char* GetName() const = 0;

        /// @brief パスのセットアップ（リソース準備など）
        /// @param context レンダリングコンテキスト
        virtual void Setup([[maybe_unused]] const RenderContext& context) {}

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        virtual void Execute(const RenderContext& context) = 0;

        /// @brief パスのクリーンアップ（リソース解放など）
        /// @param context レンダリングコンテキスト
        virtual void Cleanup([[maybe_unused]] const RenderContext& context) {}

        /// @brief 前のパスからの入力を設定
        /// @param input 前のパスの出力
        virtual void SetInput([[maybe_unused]] const PassOutput& input) {}

        /// @brief このパスの出力を取得
        /// @return パスの出力
        virtual PassOutput GetOutput() const { return output_; }

        /// @brief パスが有効かどうか
        /// @return 有効な場合true
        virtual bool IsEnabled() const { return enabled_; }

        /// @brief パスの有効/無効を設定
        /// @param enabled 有効にする場合true
        void SetEnabled(bool enabled) { enabled_ = enabled; }

    protected:
        bool enabled_ = true;
        PassOutput output_;  ///< このパスの出力
    };
}
