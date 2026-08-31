#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief アトラス上の矩形を切り出す棚（シェルフ）アロケータ
    /// @details
    ///  「同じ高さのグリフを横一列に並べ、埋まったら次の段へ移る」だけの単純な方式。
    ///  実行時にグリフが 1 つずつ増えていく用途では、
    ///  事前に全部の大きさを知る必要がある方式（MaxRects 等）が使えないので
    ///  この形が素直に合う。
    ///
    ///  1 枚を使い切ったら次の枚（Texture2DArray のスライス）へ送る。
    ///  枚数は先に決め打つ（D3D12 は配列テクスチャを後から伸ばせない）。
    ///
    /// @note 解放はできない（追加専用）。フォントの寿命＝アトラスの寿命として扱う。
    ///       全枚数を使い切ったら Allocate が false を返すので、
    ///       呼び出し側は .notdef へ倒す。
    class MsdfAtlasAllocator
    {
    public:
        /// @brief 棚の進み具合。ディスクキャッシュへ保存して復元するために公開する
        /// @details 枚は順に埋めていくので、いま使っている枚の情報だけで足りる
        ///          （それより前の枚は埋まりきっている）。
        struct State
        {
            int32_t pageIndex = 0;
            int32_t cursorX = 0;
            int32_t cursorY = 0;
            int32_t shelfHeight = 0;
        };

        /// @brief 初期化（空の状態に戻す）
        /// @param width 1 枚あたりの幅（px）
        /// @param height 1 枚あたりの高さ（px）
        /// @param pageCount 枚数
        /// @param padding グリフ同士の余白（px）。距離場の滲みを防ぐため 1 以上
        void Initialize(int width, int height, int pageCount, int padding);

        /// @brief 保存しておいた進み具合を復元する
        /// @note Initialize の後に呼ぶこと
        void Restore(const State& state);

        State GetState() const { return { pageIndex_, cursorX_, cursorY_, shelfHeight_ }; }

        /// @brief 矩形を 1 つ切り出す
        /// @param width 必要な幅（px）
        /// @param height 必要な高さ（px）
        /// @param outPage 切り出した枚の添字
        /// @param outX 切り出した左上 X
        /// @param outY 切り出した左上 Y
        /// @return 全ての枚を使い切っていたら false
        bool Allocate(int width, int height, int& outPage, int& outX, int& outY);

        /// @brief 実際に使い始めた枚数（1 以上）
        /// @details キャッシュへ書き出す枚数の決定に使う。
        ///          確保だけして未使用の枚まで保存しても無駄なので。
        int GetUsedPageCount() const { return pageIndex_ + 1; }

        /// @brief 全体の使用量（0..1）。デバッグ表示用のおおよその値
        float GetOccupancy() const;

        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }
        int GetPageCount() const { return pageCount_; }

    private:
        int32_t width_ = 0;
        int32_t height_ = 0;
        int32_t pageCount_ = 1;
        int32_t padding_ = 1;

        int32_t pageIndex_ = 0;
        int32_t cursorX_ = 0;
        int32_t cursorY_ = 0;
        int32_t shelfHeight_ = 0;
    };
}
