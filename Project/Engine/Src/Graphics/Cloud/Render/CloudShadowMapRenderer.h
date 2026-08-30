#pragma once

namespace CoreEngine
{
    struct CloudRenderContext;

    /// @brief 太陽方向の雲透過率マップを生成する
    /// @details Deferred ライティング（シーンへ落ちる雲影）とゴッドレイの双方が読むため、
    ///          ライティングより前のフェーズで走る。
    class CloudShadowMapRenderer {
    public:
        /// @brief 雲シャドウマップを焼き、SRV 状態にして返す
        void Render(const CloudRenderContext& ctx);
    };
}
