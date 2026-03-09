#pragma once
#include "RenderPass.h"
#include <functional>

namespace CoreEngine
{
    /// @brief ジオメトリ描画パス（オフスクリーンレンダリング）
    class GeometryPass : public RenderPass {
    public:
        GeometryPass() = default;
        ~GeometryPass() override = default;

        const char* GetName() const override { return "Geometry"; }

        void Execute(const RenderContext& context) override;

        /// @brief シーン固有の描画コールバックを設定
        /// @param callback 描画コールバック関数
        void SetRenderCallback(std::function<void()> callback) {
            renderCallback_ = callback;
        }

        /// @brief 使用するオフスクリーンターゲットのインデックスを設定
        /// @param index オフスクリーンインデックス（0=1枚目、1=2枚目）
        void SetOffscreenIndex(int index) {
            offscreenIndex_ = index;
        }

    private:
        std::function<void()> renderCallback_;
        int offscreenIndex_ = 0;
    };
}
