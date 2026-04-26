#pragma once

#include "IEngineSubsystem.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"

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

        /// @brief RT シャドウのディスパッチ（全ディレクショナルライト分）
        /// @details DispatchRays → テンポラル蓄積 → A-Trous デノイズの 3 ステージを実行する。
        ///　リソースバリア遷移は内部で処理する。
        void DispatchRTShadow(
            const RenderContext& context,
            DirectXCommon* dx,
            ID3D12GraphicsCommandList* cmdList,
            RayTracingShadowManager::ViewID viewId);
    };
}
