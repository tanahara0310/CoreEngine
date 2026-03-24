#pragma once
#include "BaseModelRenderer.h"

namespace CoreEngine
{
    /// @brief スキニングモデル描画用レンダラー
    class SkinnedModelRenderer : public BaseModelRenderer {
    public:
        /// @brief シェーダーコンパイル・リフレクション・RootSignature・PSO を構築（スキニング専用シェーダー使用）
        void Initialize(ID3D12Device* device) override;
        /// @brief 描画パスタイプを返す（SkinnedModel）
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }
    };
}
