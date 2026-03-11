#pragma once
#include "RenderPass.h"
#include <functional>
#include <string>

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

        /// @brief レンダーターゲット名を設定
        /// @param name ターゲット名
        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        /// @brief 設定されているターゲット名を取得
        /// @return ターゲット名
        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

    private:
        std::function<void()> renderCallback_;
        std::string targetName_ = "Offscreen0";  ///< デフォルトターゲット名
    };
}
