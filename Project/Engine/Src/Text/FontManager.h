#pragma once

#include "Text/MsdfFont.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class GraphicsCore;
    class ThreadPool;

    /// @brief MSDF フォントの所有と共有を一元管理するエンジンサービス
    /// @details
    ///  シーンが `MsdfFont` を直接 `unique_ptr` で持つと、
    ///   - シーンをまたぐたびにアトラスが焼き直される
    ///   - 同じフォントを複数シーンで共有できない
    ///   - キャッシュの置き場所が決まらない
    ///  という問題が出るため、所有をエンジン側へ引き上げる。
    ///
    ///  `TextureManager` / `ModelManager` と同じ位置づけで、
    ///  `EngineSystem::GetService<FontManager>()` から引く。
    ///
    /// @note 返す `MsdfFont*` の寿命は FontManager が持つ。シーンは参照するだけ。
    class FontManager
    {
    public:
        FontManager() = default;
        ~FontManager();

        FontManager(const FontManager&) = delete;
        FontManager& operator=(const FontManager&) = delete;

        /// @brief 初期化
        /// @param graphicsCore デバイスとディスクリプタの供給元
        void Initialize(GraphicsCore* graphicsCore);

        /// @brief 全フォントを破棄する
        /// @note GPU の処理完了後に呼ぶこと（ディスクリプタを解放するため）
        void Finalize();

        /// @brief フォントを取得する（同じ指定なら同じインスタンスを返す）
        /// @param desc 生成指定
        /// @return 生成済みフォント。失敗したら nullptr
        /// @details 初回だけ実際に構築する。2 回目以降は生成済みのものを返すので、
        ///          シーンの初期化で気軽に呼んでよい。
        MsdfFont* Acquire(const MsdfFontDesc& desc);

        /// @brief 保持しているフォント数（デバッグ表示用）
        size_t GetFontCount() const;

    private:
        /// @brief 生成指定から「同じ要求か」を判定するためのハッシュを作る
        /// @note ディスクキャッシュのキー（MsdfFontCache::ComputeKey）とは別物。
        ///       あちらは *実際に開けたフォント* を含むが、こちらは要求そのものを見る
        static uint64_t ComputeRequestHash(const MsdfFontDesc& desc);

        GraphicsCore* graphicsCore_ = nullptr;
        std::unordered_map<uint64_t, std::unique_ptr<MsdfFont>> fonts_;
        mutable std::mutex mutex_;

        /// @brief 実行時グリフのベイクを回すワーカー
        /// @details
        ///  MsdfFont はバッチ内のグリフをこのプールへ並列に投げる。
        ///  1 本はキューの取りまとめに使われるので、実際に焼くのは残り。
        ///  距離場の計算は CPU を食うため、増やしすぎると描画と食い合う。
        std::unique_ptr<ThreadPool> bakeThreadPool_;

        /// @brief ベイク用ワーカー数
        static constexpr uint32_t kBakeThreadCount = 4;
    };
}
