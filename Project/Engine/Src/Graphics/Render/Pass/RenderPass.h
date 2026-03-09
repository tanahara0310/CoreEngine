#pragma once
#include <string>

namespace CoreEngine
{
    class DirectXCommon;
    class Render;
    class RenderManager;
    class PostEffectManager;
    class LightManager;

    /// @brief レンダリングパスのコンテキスト情報
    struct RenderContext {
        DirectXCommon* dxCommon = nullptr;
        Render* render = nullptr;
        RenderManager* renderManager = nullptr;
        PostEffectManager* postEffectManager = nullptr;
        LightManager* lightManager = nullptr;
    };

    /// @brief レンダリングパスの基底クラス
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        /// @brief パス名を取得
        /// @return パス名
        virtual const char* GetName() const = 0;

        /// @brief パスのセットアップ（リソース準備など）
        /// @param context レンダリングコンテキスト
        virtual void Setup([[maybe_unused]] const RenderContext& context) {}

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        virtual void Execute(const RenderContext& context) = 0;

        /// @brief パスのクリーンアップ（リソース解放など）
        /// @param context レンダリングコンテキスト
        virtual void Cleanup([[maybe_unused]] const RenderContext& context) {}

        /// @brief パスが有効かどうか
        /// @return 有効な場合true
        virtual bool IsEnabled() const { return enabled_; }

        /// @brief パスの有効/無効を設定
        /// @param enabled 有効にする場合true
        void SetEnabled(bool enabled) { enabled_ = enabled; }

    protected:
        bool enabled_ = true;
    };
}
