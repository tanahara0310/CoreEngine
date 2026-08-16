#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

struct Particle;

/// @brief パーティクルの色モジュール
/// 注意: 初期色の設定はMainModuleで行います
/// このモジュールは色の変化（グラデーション）のみを担当します

namespace CoreEngine
{
class ColorModule : public ParticleModule {
public:
    struct ColorOverLifetime {
        Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };   // 終了色
        bool useGradient = true;   // グラデーションを使用するか
    };

    ColorModule();
    ~ColorModule() = default;

    /// @brief 色データを設定
    /// @param data 色データ
    void SetColorData(const ColorOverLifetime& data) { colorData_ = data; }

    /// @brief 色データを取得
    /// @return 色データの参照
    const ColorOverLifetime& GetColorData() const { return colorData_; }

    /// @brief パーティクルの色を更新
    /// @param particle 対象のパーティクル
    void UpdateColor(Particle& particle);

#ifdef USE_IMGUI
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    ColorOverLifetime colorData_;
};
}
