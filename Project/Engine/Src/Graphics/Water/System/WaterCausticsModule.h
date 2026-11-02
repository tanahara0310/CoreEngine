#pragma once

namespace CoreEngine
{
    struct WaterFrameContext;

    /// @brief 水底のコースティクス（スクリーンスペースとレイトレーシング）へ設定を渡す
    class WaterCausticsModule {
    public:
        /// @brief 見た目の値と方式をコースティクスの描画へ、屈折率と吸収係数をレイトレーシングの側へ渡す
        void ApplySettings(const WaterFrameContext& frame) const;
    };
}
