#pragma once
#include "RenderPass.h"
#include <functional>

namespace CoreEngine
{
    class RenderTarget;

    /// @brief ジオメトリ描画パス（オフスクリーンレンダリング）
    class GeometryPass : public RenderPass {
    public:
        GeometryPass() = default;
        ~GeometryPass() override = default;

        const char* GetName() const override { return "Geometry"; }

        void Execute(const RenderContext& context) override;

        /// @brief このパスの出力を取得
        PassOutput GetOutput() const override { return output_; }

        /// @brief シーン固有の描画コールバックを設定
        /// @param callback 描画コールバック関数
        void SetRenderCallback(std::function<void()> callback) {
            renderCallback_ = callback;
        }

        /// @brief レンダーターゲットを設定
        /// @param target レンダーターゲット
        void SetRenderTarget(RenderTarget* target) {
            renderTarget_ = target;
        }

    private:
        std::function<void()> renderCallback_;
        RenderTarget* renderTarget_ = nullptr;
    };
}
