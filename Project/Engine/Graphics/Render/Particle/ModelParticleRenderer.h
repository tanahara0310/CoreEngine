#pragma once

#include "BaseParticleRenderer.h"
#include <d3d12.h>
#include <wrl.h>

// 前方宣言
class ParticleSystem;

/// @brief モデルパーティクル専用レンダラー

namespace CoreEngine
{
class ModelParticleRenderer : public BaseParticleRenderer {
public:
    ModelParticleRenderer() = default;
    ~ModelParticleRenderer() override = default;

    /// @brief このレンダラーがサポートする描画タイプを取得
    /// @return 描画パスタイプ
    RenderPassType GetRenderPassType() const override { return RenderPassType::ModelParticle; }

    /// @brief モデルパーティクルを描画
    /// @param particle パーティクルシステム
    void Draw(ParticleSystem* particle) override;

protected:
    /// @brief パイプラインステートオブジェクトの作成（モデル用）
    void CreatePSO() override;
};
}
