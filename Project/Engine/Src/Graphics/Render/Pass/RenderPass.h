#pragma once
#include <string>
#include <d3d12.h>

namespace CoreEngine
{
    class DirectXCommon;
    class RenderManager;
    class PostEffectManager;
    class LightManager;
    class RenderTargetManager;

    /// @brief レンダリングパスのコンテキスト情報
    struct RenderContext {
        DirectXCommon* dxCommon = nullptr;
        RenderManager* renderManager = nullptr;
        PostEffectManager* postEffectManager = nullptr;
        LightManager* lightManager = nullptr;
        RenderTargetManager* renderTargetManager = nullptr;  ///< レンダーターゲット管理（Phase 1で追加）
    };

    /// @brief パス間のデータ受け渡し用構造体
    struct PassOutput {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};  ///< 出力テクスチャのSRVハンドル
        ID3D12Resource* resource = nullptr;        ///< 出力リソース（オプション）
        bool isValid = false;                      ///< 有効なデータかどうか

        /// @brief 出力をリセット
        void Reset() {
            srvHandle = {};
            resource = nullptr;
            isValid = false;
        }
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

        /// @brief 前のパスからの入力を設定
        /// @param input 前のパスの出力
        virtual void SetInput([[maybe_unused]] const PassOutput& input) {}

        /// @brief このパスの出力を取得
        /// @return パスの出力
        virtual PassOutput GetOutput() const { return output_; }

        /// @brief パスが有効かどうか
        /// @return 有効な場合true
        virtual bool IsEnabled() const { return enabled_; }

        /// @brief パスの有効/無効を設定
        /// @param enabled 有効にする場合true
        void SetEnabled(bool enabled) { enabled_ = enabled; }

    protected:
        bool enabled_ = true;
        PassOutput output_;  ///< このパスの出力
    };
}
