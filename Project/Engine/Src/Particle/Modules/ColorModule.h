#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

struct Particle;

namespace CoreEngine
{
/// @brief ライフタイムに沿った色変化の設定（ColorModule）
struct ParticleColorData {
    Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };   // 終了色
    bool useGradient = true;   // グラデーションを使用するか

    /// 色の変化を始める寿命の割合（0.0〜1.0）
    /// @details 0 なら生まれた瞬間から終了色へ向かって変わり始める。
    ///          0.7 にすると寿命の最後 30% だけで変化するので、
    ///          「消え際だけフェードさせる」用途に使える。
    float startRatio = 0.0f;
};

/// @brief パーティクルの色モジュール
/// 注意: 初期色の設定はMainModuleで行います
/// このモジュールは色の変化（グラデーション）のみを担当します
class ColorModule : public ParticleModuleT<ParticleColorData> {
public:
    using ColorOverLifetime = ParticleColorData;

    ColorModule();
    ~ColorModule() = default;

    /// @brief パーティクルの色を更新
    /// @param particle 対象のパーティクル
    void UpdateColor(Particle& particle);

#ifdef CORE_EDITOR
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui();
#endif
};
}
