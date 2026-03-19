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

        /// @brief Execute() 開始時にレンダーターゲットをクリアするか設定する
        /// @param enabled true: クリアする（デフォルト） / false: クリアしない
        /// @note DeferredLightingPass などで前段に書き込み済みのコンテンツを上書きしたくない場合に false を指定する
        void SetClearEnabled(bool enabled) { clearEnabled_ = enabled; }

        /// @brief クリアが有効かどうか取得
        bool IsClearEnabled() const { return clearEnabled_; }

    private:
        std::function<void()> renderCallback_;
        std::string targetName_ = "Offscreen0";  ///< デフォルトターゲット名

        /// @brief レンダーターゲットをクリアするかどうか
        /// false にすると DeferredLightingPass などで書き込んだ内容を保持する
        bool clearEnabled_ = true;
    };
}
