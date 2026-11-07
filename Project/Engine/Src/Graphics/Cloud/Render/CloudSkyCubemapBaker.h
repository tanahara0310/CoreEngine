#pragma once

namespace CoreEngine
{
    struct CloudRenderContext;

    /// @brief 空キューブマップへの雲の焼き込み（スペキュラ IBL / 水面の雲反射用）
    class CloudSkyCubemapBaker {
    public:
        /// @brief 空キューブマップへ雲を前乗算合成する
        /// @details 雲をマーチするのは gCloud.cubeCaptureFaceFirst から cubeCaptureFaceCount 面だけ。
        ///          残りの面は雲だけのキューブ（CloudResources::cloudCube）に残っている値を合成する。
        /// @warning CaptureSkyEnvironment の直後・PrefilterSkyEnvironment の前に呼ぶこと
        ///          （キューブマップは UAV 状態が前提）
        /// @return 記録したら true（空キューブマップの UAV が無ければ false）
        bool Bake(const CloudRenderContext& ctx);
    };
}
