#pragma once

namespace CoreEngine
{
    struct CloudRenderContext;

    /// @brief 雲が読む静的テクスチャ（ノイズ 3 枚と配置ペイント）の更新
    /// @details ノイズは純手続き生成なので初回のみ。配置ペイントは CPU 側が変更した
    ///          ときだけアップロードバッファから転送する。
    class CloudNoiseBaker {
    public:
        /// @brief ダーティなら生成・転送し、レイマーチが読める SRV 状態にする
        void BakeIfNeeded(const CloudRenderContext& ctx);

        /// @brief 配置ペイントの変更を GPU へ反映させる
        void MarkPaintDirty() { paintDirty_ = true; }

        /// @brief 生成済みか（レイマーチ側の実行可否判定に使う）
        bool IsReady() const { return generated_; }

    private:
        /// @brief アップロードバッファから配置ペイントテクスチャへ転送する
        void UploadPaintTexture(const CloudRenderContext& ctx);

        bool dirty_ = true;
        bool paintDirty_ = true;
        bool generated_ = false;
    };
}
