#pragma once

namespace CoreEngine
{
    /// @brief レンダリング最適化のデバッグトグル集約
    /// @details 視錐台カリング・LOD・Hi-Zオクルージョンなど個別の最適化を
    ///          実行時に単独でオン/オフし、問題の切り分けをできるようにする。
    ///          Hi-Zの有効/無効はGPUリソースの状態と結びつくため
    ///          HiZOcclusionSystem::SetEnabled が引き続き管理する。
    struct RenderOptimizationSettings {
        bool frustumCullingEnabled = true; ///< オブジェクト単位の視錐台カリング
        bool lodEnabled = true;            ///< 画面占有率ベースのLOD選択（無効時は常にLOD0）
        int  forcedLodIndex = -1;          ///< 0以上で全モデルをそのLODに強制（A/B計測用、-1=自動）

    /// @brief プロセス共有のインスタンスを取得
        static RenderOptimizationSettings& Get() {
            static RenderOptimizationSettings instance;
            return instance;
        }

    };
}  // namespace CoreEngine
