#pragma once
#include "BaseModelRenderer.h"

namespace CoreEngine
{
    /// @brief 通常モデル描画用レンダラー
    class ModelRenderer : public BaseModelRenderer {
    public:
        /// @brief シェーダーコンパイル・リフレクション・RootSignature・PSO を構築
        void Initialize(ID3D12Device* device) override;
        /// @brief 描画パスタイプを返す（Model）
        RenderPassType GetRenderPassType() const override { return RenderPassType::Model; }
    };
}
