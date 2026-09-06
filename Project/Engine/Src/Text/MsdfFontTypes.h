#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief 1 グリフ分のアトラス配置とレイアウト情報
    /// @details
    ///  座標は全て **em 単位**（フォントサイズ 1.0 のときの大きさ）で持つ。
    ///  表示サイズ（px）を掛けるだけでピクセル座標になるので、
    ///  フォントサイズを変えても頂点を組み直す必要が無い。
    ///  これが「スケールを変えても輪郭が崩れない」の C++ 側の担保にあたる。
    ///
    /// @warning この構造体はディスクキャッシュへ生のまま書き出す。
    ///          メンバを変えたら MsdfFontCache の版を上げること。
    struct MsdfGlyph
    {
        /// @name 描画クワッドの境界（ベースライン原点・Y 上正・em 単位）
        /// @note 距離場のマージン（pxRange の半分）を含んだ大きさ。
        ///       字面より少し大きいのが正しい。ここを詰めると縁が欠ける。
        /// @{
        float planeLeft = 0.0f;
        float planeBottom = 0.0f;
        float planeRight = 0.0f;
        float planeTop = 0.0f;
        /// @}

        /// @name アトラス上の UV（左上原点・0..1）
        /// @{
        float uvLeft = 0.0f;
        float uvTop = 0.0f;
        float uvRight = 0.0f;
        float uvBottom = 0.0f;
        /// @}

        /// @brief 字送り幅（em 単位）
        float advance = 0.0f;

        /// @brief アトラス配列の何枚目に置かれているか
        /// @details アトラスは Texture2DArray なので、UV に加えて枚数の添字が要る。
        ///          頂点の texcoord.z へそのまま流す。
        uint32_t page = 0;

        /// @brief アトラスに絵を持つか（空白文字は false で、advance だけ使う）
        bool hasBitmap = false;
    };

    /// @brief フォント全体の縦組みメトリクス（em 単位）
    struct MsdfFontMetrics
    {
        float ascender = 0.0f;   ///< ベースラインから上端まで（正）
        float descender = 0.0f;  ///< ベースラインから下端まで（負）
        float lineHeight = 0.0f; ///< 行送り
    };

    /// @brief アトラス生成時のパラメータ
    struct MsdfBakeSettings
    {
        /// @brief 1em あたりのピクセル数（＝グリフを焼く解像度）
        /// @note 32 以下だと画数の多い漢字でストローク同士の距離場が干渉して
        ///       斑点状のアーティファクトが出る。品質に効くのは pxRange より
        ///       こちらで、上げるほど msdfgen のエラー訂正の出番が減る。
        int glyphPixelSize = 56;

        /// @brief 距離場の有効範囲（px）
        /// @note **縁取りの太さの上限はここで決まる**。距離場は輪郭の外側
        ///       pxRange/2 px しか情報を持たないので、それより太い縁取りは
        ///       そもそも描けない（無理に描くとクワッドの端で切れて矩形が出る）。
        ///       実際に使える最大幅は kMaxOutlineSd * pxRange / glyphPixelSize em。
        ///       この設定なら 0.375 * 12 / 56 = 0.08em（44px 表示で約 3.5px）。
        ///
        ///       「和文で pxRange を上げるとグリフが壊れる」と言われるが、
        ///       画数の多い漢字（鬱鑑龍議織藤曜纏躍髪鏡醸麗驚）で実測した限り
        ///       エラー訂正が書き換えた画素の割合は 40px/4 で 3.36%、
        ///       56px/12 で 2.76% と、むしろ減っている。
        float pxRange = 12.0f;

        int atlasWidth = 1024;  ///< 1 枚あたりの幅（px）
        int atlasHeight = 1024; ///< 1 枚あたりの高さ（px）

        /// @brief アトラスの枚数（Texture2DArray のスライス数）
        /// @details 1 枚が埋まったら次の枚へ送る。D3D12 は配列テクスチャを
        ///          後から伸ばせないので、上限をここで先に確保する。
        ///          VRAM は width * height * 4 * pageCount（1024²×4枚 = 16MB）。
        ///          和文を数千字扱うなら 4 枚は見ておくこと。
        int atlasPageCount = 4;

        /// @brief グリフ同士の余白（px）
        /// @note 隣のグリフの距離場が滲むのを防ぐ。1 以上必須。
        int padding = 2;
    };
}
