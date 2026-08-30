#pragma once

namespace CoreEngine
{
    struct CloudRenderContext;

    /// @brief 空キューブマップへの雲の焼き込み（スペキュラ IBL / 水面の雲反射用）
    class CloudSkyCubemapBaker {
    public:
        /// @brief 空キューブマップへ雲を前乗算合成する
        /// @warning CaptureSkyEnvironment の直後・PrefilterSkyEnvironment の前に呼ぶこと
        ///          （キューブマップは UAV 状態が前提）
        void Bake(const CloudRenderContext& ctx);
    };
}
