#pragma once

namespace CoreEngine
{
    struct CloudRenderContext;

    /// @brief ノイズテクスチャ 3 枚（BaseShape / Detail / WeatherMap）の生成
    /// @details 手続き生成なので実行時パラメータに依存しない。ダーティ時のみ 1 度走る。
    class CloudNoiseBaker {
    public:
        /// @brief ダーティなら 3 枚を生成し、レイマーチが読める SRV 状態にする
        void BakeIfNeeded(const CloudRenderContext& ctx);

        /// @brief 生成済みか（レイマーチ側の実行可否判定に使う）
        bool IsReady() const { return generated_; }

    private:
        bool dirty_ = true;
        bool generated_ = false;
    };
}
