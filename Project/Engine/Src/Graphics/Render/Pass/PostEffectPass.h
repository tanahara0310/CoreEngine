#pragma once
#include "RenderPass.h"
#include "Graphics/PostEffect/Effect/PostEffectBase.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#include <d3d12.h>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief ポストエフェクトの 1 パスを実行するグラフノード
    /// @details 「どのエフェクトか」は知らない。エフェクトが PostEffectGraphBuilder へ積んだ
    ///          PostEffectStep を 1 つ受け取り、宣言どおりに読み書きして record を呼ぶだけ。
    ///          エフェクト固有の分岐をここへ書きたくなったら、それは
    ///          PrepareFrame か DeclareExtraInputs の設計漏れである。
    class PostEffectPass : public RenderPass {
    public:
        PostEffectPass() = default;
        ~PostEffectPass() override = default;

        const char* GetName() const override { return "PostEffect"; }

        /// @brief 実行対象のステップを設定する
        /// @note ステップはコピーして持つ（呼び出し側のコンテナ再確保でダングリングしないため）
        void SetStep(PostEffectBase* effect, const std::string& effectName, const PostEffectStep& step);

        const std::string& GetInputResourceName() const { return primaryInputName_; }
        const std::string& GetOutputResourceName() const { return step_.write; }

        /// @brief ステップの宣言どおりに入出力を Graph へ登録する
        /// @note step 未設定の placeholder インスタンスは何も宣言しない
        void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;

        bool IsEnabledForView(const RenderViewSettings& view) const override { return view.enablePostEffect; }

        void Execute(const RenderContext& context) override;

    private:
        /// @brief 申告された追加入力を Blackboard から解決してエフェクトへ渡す
        /// @return 必須入力が全て揃っていれば true
        bool ResolveExtraInputs(const RenderContext& context);

        PostEffectBase* effect_ = nullptr;
        std::string     effectName_;
        PostEffectStep  step_;

        /// @brief ステップの先頭 read（デバッグ表示・従来 API 互換のため保持）
        std::string primaryInputName_;
    };
}
