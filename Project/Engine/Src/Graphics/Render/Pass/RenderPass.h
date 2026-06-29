#pragma once
#include <string>
#include <d3d12.h>

#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    /// @brief RenderGraph が扱う描画ビュー種別
    enum class RenderViewType : uint32_t {
        GameView = 0,
        ReflectionView = 1,
        CaptureView = 2,
    };

    /// @brief View ごとの Graph 実行設定
    struct RenderViewSettings {
        RenderViewType viewType = RenderViewType::GameView;
        bool enableSSAO = true;
        bool enableRTShadow = true;
        bool enablePostEffect = true;
        bool enableBackBuffer = true;
        std::string sceneColorTargetName = RenderTargetNames::SceneColor;
    };

    class DirectXCommon;
    class RenderManager;
    class RayTracingSubsystem;
    class SceneManager;
    class PostEffectManager;
    class RenderingTechniqueManager;
    class LightManager;
    class RenderTargetManager;
    class GBufferManager;
    class ShadowMapManager;
    class AccelerationStructureManager;
    class RayTracingShadowManager;
    class WaterCausticsRayTracingManager;
    class WaterRefractionRayTracingManager;
    class FFTOceanManager;
    class CameraManager;
    class DepthStencilManager;
    /// @brief レンダリングパスのコンテキスト情報
    struct RenderContext {
        DirectXCommon* dxCommon = nullptr;
        RenderManager* renderManager = nullptr;
        RayTracingSubsystem* rayTracingSubsystem = nullptr;
        SceneManager* sceneManager = nullptr;
        PostEffectManager* postEffectManager = nullptr;
        RenderingTechniqueManager* renderingTechniqueManager = nullptr; ///< レンダリング技術管理（SSAO・TAA等）
        LightManager* lightManager = nullptr;
        ShadowMapManager* shadowMapManager = nullptr;  ///< シャドウマップ管理（LVP記列・ SRV 取得用）
        RenderTargetManager* renderTargetManager = nullptr;
        GBufferManager* gBufferManager = nullptr;  ///< G-Buffer管理（Deferred）
        AccelerationStructureManager* accelerationStructureManager = nullptr; ///< DXR 加速構造管理
        RayTracingShadowManager* rtShadowManager = nullptr; ///< DXR レイトレーシングシャドウ
        WaterCausticsRayTracingManager* rtWaterCausticsManager = nullptr; ///< DXR 水面コースティクス
        WaterRefractionRayTracingManager* rtWaterRefractionManager = nullptr; ///< DXR 水面屈折
        FFTOceanManager* fftOceanManager = nullptr; ///< FFT Ocean 波面生成マネージャー
        CameraManager* cameraManager = nullptr; ///< カメラ管理（SSAO等でビュー/プロジェクション行列取得用）
        DepthStencilManager* depthStencilManager = nullptr; ///< 深度ステンシル管理（バリア遷移・クリアを一元管理）
        FrameBlackboard* frameBlackboard = nullptr; ///< フレーム内共有リソースの論理名管理
        const WaterSurfaceData* waterRefractionSurfaceData = nullptr; ///< DXR 水面屈折用の波面データ
        RenderViewSettings viewSettings{}; ///< 現在の View 種別と有効化するパス群設定
        uint32_t currentRTShadowViewId = static_cast<uint32_t>(RenderViewType::GameView); ///< 現在の RT シャドウビュー
    };

    /// @brief レンダリングパスの基底クラス
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
