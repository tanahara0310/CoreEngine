#pragma once

#include "IEngineSubsystem.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/WaterSurfaceData.h"

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    struct RenderContext;
    class DirectXCommon;
    class ModelManager;
    class SceneManager;

    /// @brief DXR (DirectX Raytracing) 関連処理を担当するサブシステム
    /// @details EngineSystem::ExecuteRenderPipeline から DXR の BLAS/TLAS 構築および
    ///　RT シャドウディスパッチ処理を分離する。状態は保持しない（処理委譲のみ）。
    class RayTracingSubsystem : public IEngineSubsystem
    {
    public:
        RayTracingSubsystem() = default;
        ~RayTracingSubsystem() override = default;

        RayTracingSubsystem(const RayTracingSubsystem&) = delete;
        RayTracingSubsystem& operator=(const RayTracingSubsystem&) = delete;

        const char* GetName() const noexcept override { return "RayTracingSubsystem"; }

        /// @brief フレーム開始時の DXR 加速構造構築
        /// @details 未構築モデルリソースの BLAS 遅延ビルドと、シーン内 ModelGameObject からの
        ///　TLAS 構築を行う。RT シャドウマネージャのフレーム状態リセットも実施。
        void BuildAccelerationStructures(
            const RenderContext& context,
            DirectXCommon* dx,
            ModelManager* modelManager,
            SceneManager* sceneManager);

        /// @brief RT シャドウの DispatchRays ステージ（全ディレクショナルライト分）
        /// @details RenderGraph 上では RTShadowTracePass として実行される。
        ///　ライト別の内部リソースバリアはマネージャ内で処理する。
        void DispatchRTShadowTrace(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            RayTracingShadowManager::ViewID viewId);

        /// @brief RT シャドウのテンポラル蓄積ステージ（全ディレクショナルライト分）
        /// @details RenderGraph 上では RTShadowTemporalPass として実行される。
        void DispatchRTShadowTemporal(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            RayTracingShadowManager::ViewID viewId);

        /// @brief RT シャドウの A-Trous デノイズステージ（全ディレクショナルライト分）
        /// @details RenderGraph 上では RTShadowDenoisePass として実行される。
        void DispatchRTShadowDenoise(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            RayTracingShadowManager::ViewID viewId);

        /// @brief DXR 水面屈折のディスパッチ
        void DispatchWaterRefraction(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            WaterRefractionRayTracingManager::ViewID viewId,
            const WaterSurfaceData& surfaceData);

        /// @brief DXR 水面コースティクスのディスパッチ
        void DispatchWaterCaustics(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            WaterCausticsRayTracingManager::ViewID viewId,
            const WaterSurfaceData& surfaceData);
    };
}
