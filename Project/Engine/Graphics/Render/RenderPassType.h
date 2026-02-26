#pragma once

/// @brief 描画パスタイプ

namespace CoreEngine
{
enum class RenderPassType {
    Invalid = -1,        // 無効
    ShadowMap = 0,       // シャドウマップ生成（最優先）
    Model,               // 通常モデル
    SkinnedModel,        // スキニングモデル
    SkyBox,              // SkyBox
    ModelParticle,       // モデルパーティクル（3D）
    Line,                // ライン描画（デバッグ用）
    Particle,            // パーティクル（ビルボード）
    Sprite,              // スプライト（最前面）
    Text,                // テキスト描画
};
}
