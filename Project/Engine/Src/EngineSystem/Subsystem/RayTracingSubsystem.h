#pragma once

#include "IEngineSubsystem.h"

namespace CoreEngine
{
    struct RenderContext;
    class GraphicsCore;
    class SceneManager;

    /// @brief DXR (DirectX Raytracing) の加速構造を作るサブシステム
    /// @details 効果（影・水面など）は RTEffectSet が持ち、効果のパスが撃つ。ここは状態を持たない。
    class RayTracingSubsystem : public IEngineSubsystem
    {
    public:
        RayTracingSubsystem() = default;
        ~RayTracingSubsystem() override = default;

        RayTracingSubsystem(const RayTracingSubsystem&) = delete;
        RayTracingSubsystem& operator=(const RayTracingSubsystem&) = delete;

        const char* GetName() const noexcept override { return "RayTracingSubsystem"; }

        /// @brief フレーム開始時の DXR 加速構造構築
        /// @details 効果へフレームの頭を知らせてから、RTScene がシーンの物を集め、使うモデルの BLAS を作り、TLAS を組む。
        void BuildAccelerationStructures(
            const RenderContext& context,
            GraphicsCore* dx,
            SceneManager* sceneManager);
    };
}
