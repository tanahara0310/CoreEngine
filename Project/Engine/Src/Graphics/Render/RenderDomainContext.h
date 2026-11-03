#pragma once

#include <cstdint>
#include <memory>

#include "Graphics/RHI/IResizable.h"

namespace CoreEngine
{
    class ShaderProgramCache;

    class GraphicsCore;
    class DescriptorAllocator;
    class SceneDepth;
    class GBufferManager;
    class AccelerationStructureManager;
    class VertexAnimationDeformer;
    class RayTracingShadowManager;
    class WaterRefractionRayTracingManager;
    class WaterReflectionRayTracingManager;
    class WaterCausticsRayTracingManager;
    class WaterSeabedRayTracingManager;
    class WaterSeaMapRayTracingManager;
    class FFTOceanManager;
    class WaterShoreFoam;
    class WaterWhitecapFoam;
    class WaterHeightQueryManager;
    class WaterQuery;
    class AtmosphereManager;
    class VolumetricCloudManager;
    class FogManager;
    class LightManager;
    struct WaterSurfaceData;
    struct RenderContext;

    /// @brief 描画ドメイン固有マネージャーの所有・初期化クラス
    /// @note シーン深度 / GBuffer / レイトレーシング等、
    /// GraphicsCore（D3D12インフラ層）から分離したレンダリングドメイン管理。
    /// リサイズ通知は Initialize で自分を GraphicsCore へ登録し、Shutdown / 破棄時に解除する。
    class RenderDomainContext : public IResizable {
    public:
        RenderDomainContext();
        ~RenderDomainContext() override; // 前方宣言型の unique_ptr デストラクタは .cpp に実装

        /// @brief 初期化
        /// @param dxCommon GraphicsCore（デバイス・DescriptorAllocator 取得用）
        /// @param width 初期ウィンドウ幅
        /// @param height 初期ウィンドウ高さ
        void Initialize(GraphicsCore* dxCommon, int32_t width, int32_t height,
            ShaderProgramCache* shaderProgramCache);

        /// @brief 水面の描画に使うマネージャ（水面のレイトレ 4 つ・FFT の海・泡・高さの問い合わせ）を、まだなら作る
        /// @details 水面のあるシーン（WaterSystem の初期化）が呼ぶ。作るまでは各 Get が nullptr を返す。
        ///          一度作ったら Shutdown まで持つ。
        void EnsureWaterInitialized();

        /// @brief レイトレの State Object（影と水面）を別スレッドで作っていれば、全部の完成を待つ
        /// @details 起動の最後（窓を出す前）に呼ぶ。呼ばなくても最初のディスパッチの前に待つ。
        void WaitForRayTracingPipelines();

        /// @brief シャットダウン（GPU完了後に呼ぶこと）
        void Shutdown();

        /// @brief ウィンドウリサイズ時の処理（IResizable）
        /// @param width 新しい幅
        /// @param height 新しい高さ
        void OnWindowResize(int32_t width, int32_t height) override;

        // ===== フレーム =====

        /// @brief 所有するマネージャ群を今フレームの RenderContext へ注入する
        /// @param context 注入先。該当フィールドのみ上書きする
        /// @details ドメインマネージャを 1 つ増やしたときに EngineSystem を編集せず済ませるための
        ///          唯一の注入点。以前は EngineSystem::ExecuteRenderPipeline に
        ///          `context.x = ctx ? ctx->GetX() : nullptr` が 12 行並んでいた。
        /// @note nullptr ガードは不要（RenderContext の各フィールドは nullptr 初期化済みで、
        ///       未生成のマネージャは .get() がそのまま nullptr を返す）
        void PopulateRenderContext(RenderContext& context);

        /// @brief 全 View の描画完了後に、所有するマネージャのフレーム状態を後始末する
        /// @param lightManager 大気の透過率変調の解除先（nullptr 可）
        /// @details フレーム有効化フラグを持つマネージャを増やしたときに、
        ///          EngineSystem 側へ機能名の分岐を書き足さずに済ませるための集約点。
        void EndFrame(LightManager* lightManager);

        // アクセッサ
        /// @brief メインシーンの深度（GBuffer が書き、各パスが読む。OffscreenRenderTarget の共有 DSV）
        SceneDepth* GetSceneDepth() { return sceneDepth_.get(); }
        GBufferManager* GetGBufferManager() { return gBufferManager_.get(); }
        const GBufferManager* GetGBufferManager()         const { return gBufferManager_.get(); }
        AccelerationStructureManager* GetAccelerationStructureManager() { return accelerationStructureManager_.get(); }
        RayTracingShadowManager* GetRayTracingShadowManager() { return rtShadowManager_.get(); }
        WaterRefractionRayTracingManager* GetWaterRefractionRayTracingManager() { return rtWaterRefractionManager_.get(); }
        WaterReflectionRayTracingManager* GetWaterReflectionRayTracingManager() { return rtWaterReflectionManager_.get(); }
        WaterCausticsRayTracingManager* GetWaterCausticsRayTracingManager() { return rtWaterCausticsManager_.get(); }
        WaterSeabedRayTracingManager* GetWaterSeabedRayTracingManager() { return rtWaterSeabedManager_.get(); }
        WaterSeaMapRayTracingManager* GetWaterSeaMapRayTracingManager() { return rtWaterSeaMapManager_.get(); }
        FFTOceanManager* GetFFTOceanManager() { return fftOceanManager_.get(); }
        WaterWhitecapFoam* GetWaterWhitecapFoam() { return waterWhitecapFoam_.get(); }
        WaterShoreFoam* GetWaterShoreFoam() { return waterShoreFoam_.get(); }
        /// @brief 水面の高さを聞く点の集まり（いつでもある。水面のマネージャを作るまでは結果が届かない）
        WaterQuery* GetWaterQuery() { return waterQuery_.get(); }
        WaterHeightQueryManager* GetWaterHeightQueryManager() { return waterHeightQueryManager_.get(); }
        AtmosphereManager* GetAtmosphereManager() { return atmosphereManager_.get(); }
        VolumetricCloudManager* GetVolumetricCloudManager() { return volumetricCloudManager_.get(); }
        FogManager* GetFogManager() { return fogManager_.get(); }

        // ===== 水の 1 フレームの状態 =====

        /// @brief このフレームの水面の状態と FFT の海の時刻を渡す（WaterSystem が毎フレーム呼ぶ）
        /// @param snapshot 水面の状態（水面が無ければ nullptr）。所有者は WaterSystem
        /// @param fftOceanTime FFT の海の時刻 [s]（水面の表示や波の方式の切り替えと関係なく進む）
        /// @warning WaterSystem の Finalize で必ず nullptr へ戻す
        void PublishWaterFrame(const WaterSurfaceData* snapshot, float fftOceanTime)
        {
            waterSurfaceState_ = snapshot;
            fftOceanSimulationTime_ = fftOceanTime;
        }

    private:
        GraphicsCore* dxCommon_ = nullptr; ///< リサイズ通知の登録解除に使う（非所有）
        ShaderProgramCache* shaderProgramCache_ = nullptr; ///< 水面のマネージャを後から作るときに使う（非所有）
        bool waterInitialized_ = false;    ///< 水面のマネージャを作ったか

        std::unique_ptr<SceneDepth>                   sceneDepth_;
        std::unique_ptr<GBufferManager>               gBufferManager_;
        std::unique_ptr<AccelerationStructureManager> accelerationStructureManager_;
        std::unique_ptr<VertexAnimationDeformer>      vertexAnimationDeformer_;  ///< 揺れる植物の BLAS 用の頂点変形
        std::unique_ptr<RayTracingShadowManager>      rtShadowManager_;
        std::unique_ptr<WaterRefractionRayTracingManager> rtWaterRefractionManager_;
        std::unique_ptr<WaterReflectionRayTracingManager> rtWaterReflectionManager_;
        std::unique_ptr<WaterCausticsRayTracingManager> rtWaterCausticsManager_;
        std::unique_ptr<WaterSeabedRayTracingManager> rtWaterSeabedManager_;
        std::unique_ptr<WaterSeaMapRayTracingManager> rtWaterSeaMapManager_;
        std::unique_ptr<FFTOceanManager> fftOceanManager_;
        std::unique_ptr<WaterWhitecapFoam> waterWhitecapFoam_;
        std::unique_ptr<WaterShoreFoam> waterShoreFoam_;
        std::unique_ptr<WaterQuery> waterQuery_;
        std::unique_ptr<WaterHeightQueryManager> waterHeightQueryManager_;
        std::unique_ptr<AtmosphereManager> atmosphereManager_;
        std::unique_ptr<VolumetricCloudManager> volumetricCloudManager_;
        std::unique_ptr<FogManager> fogManager_;

        /// @brief WaterSystem が渡した水面の状態（非所有）
        const WaterSurfaceData* waterSurfaceState_ = nullptr;
        /// @brief FFT の海の時刻 [s]
        float fftOceanSimulationTime_ = 0.0f;
    };

} // namespace CoreEngine
