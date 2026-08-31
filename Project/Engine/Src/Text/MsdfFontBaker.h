#pragma once

#include "Text/MsdfAtlasAllocator.h"
#include "Text/MsdfFontTypes.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class DirectWriteFontFace;

    /// @brief アトラス上の置き場所を切り出す関数
    /// @details
    ///  ベイカーがアロケータを直接持たず呼び出し側へ委ねるのは、
    ///  **並列ベイク時にロックを最小限へ絞るため**。
    ///  距離場の計算（全体の 9 割）はロックの外で走らせ、
    ///  この確保だけを守れば複数ワーカーで安全に焼ける。
    /// @return 空きが無ければ false
    using MsdfGlyphAllocateFn =
        std::function<bool(int width, int height, int& outPage, int& outX, int& outY)>;

    /// @brief グリフ 1 つを焼いた結果
    /// @details アトラスへの書き込みは呼び出し側が行う。
    ///          事前一括ベイクと実行時の追加ベイクで同じ形を使う。
    struct MsdfGlyphBitmap
    {
        bool valid = false;      ///< 焼けたか（フォントに無い / 空きが無い等で false）
        MsdfGlyph glyph;         ///< plane 境界 / UV / advance / page
        int atlasX = 0;          ///< アトラス上の配置（左上）
        int atlasY = 0;
        int width = 0;           ///< 画素サイズ
        int height = 0;
        std::vector<uint8_t> pixels; ///< RGBA8・top-down・width*height*4（空白なら空）
    };

    /// @brief アトラス生成の結果
    struct MsdfBakeResult
    {
        bool success = false;

        int atlasWidth = 0;
        int atlasHeight = 0;
        int pageCount = 0; ///< pixels に入っている枚数

        /// @brief アトラス画素（RGBA8・**top-down**・非圧縮・枚を順に連結）
        /// @details RGB に MSDF、A に真の SDF（MTSDF）が入る。
        ///          描画は median(rgb) だけで足りるが、A を持っておくと
        ///          縁取り・グロー・影を後から正確に足せる。
        std::vector<uint8_t> pixels;

        std::unordered_map<char32_t, MsdfGlyph> glyphs;

        /// @brief 未収録文字の代わりに描く豆腐（□）
        /// @details 必ず 1 つ焼く。無い文字を黙って捨てると
        ///          「文字列が部分的に消える」形で不具合が見えなくなるため。
        MsdfGlyph notdefGlyph;

        /// @brief 焼き終えた時点の棚の進み具合
        /// @details 実行時に追加でグリフを足すときは、ここから続きを切り出す。
        ///          ディスクキャッシュにも保存する。
        MsdfAtlasAllocator::State allocatorState;

        MsdfFontMetrics metrics{};
        MsdfBakeSettings settings{};

        int bakedGlyphCount = 0;   ///< アトラスに絵を置いたグリフ数
        int blankGlyphCount = 0;   ///< 空白など輪郭を持たなかったグリフ数
        int fallbackGlyphCount = 0;///< 代替フォントから拾ったグリフ数

        /// @brief どのフォントにも無い / 置き場所が無かった文字（ログ・デバッグ用）
        std::vector<char32_t> missingCodePoints;

        double bakeSeconds = 0.0;
    };

    /// @brief DirectWrite のアウトラインを msdfgen で MSDF アトラスへ焼く
    /// @details
    ///  MSDF 生成の「②エッジ彩色 → ③距離場計算 → ④矩形の切り出し」を担当する。
    ///  ②③は msdfgen/core に丸投げしており、このクラスがやるのは
    ///  座標系の橋渡しとアトラスへの配置だけ。
    ///
    /// @note 生成は重い（1 グリフあたり数 ms）。ゲーム中に同期で呼ばないこと。
    class MsdfFontBaker
    {
    public:
        /// @brief グリフを 1 つ焼いて、アトラス上の置き場所を確保する
        /// @param faceChain フォントの優先順リスト。文字ごとに先頭から探す
        /// @param codePoint 焼く文字
        /// @param settings 解像度・距離場範囲
        /// @param allocate 置き場所の切り出し（呼び出し側でロックを取ること）
        /// @param outlineMutex アウトライン取得を直列化したい場合に渡す（不要なら nullptr）
        /// @param outUsedFallback 先頭以外のフォントから拾ったら true
        /// @return 焼いた結果。どのフォントにも無い / 場所が無い場合は valid = false
        /// @note 事前一括ベイクと実行時の追加ベイクの両方がここを通る。
        ///       経路を分けると座標系の扱いがずれるので必ず 1 本にしておくこと。
        static MsdfGlyphBitmap BakeGlyph(
            const std::vector<const DirectWriteFontFace*>& faceChain,
            char32_t codePoint,
            const MsdfBakeSettings& settings,
            const MsdfGlyphAllocateFn& allocate,
            std::mutex* outlineMutex = nullptr,
            bool* outUsedFallback = nullptr);

        /// @brief `.notdef`（豆腐）を焼く
        /// @details フォントの .notdef が空だった場合は中抜き矩形を自前で組む。
        ///          何も描かないと「文字が消えた」ようにしか見えず不具合に気付けない。
        static MsdfGlyphBitmap BakeNotdef(
            const DirectWriteFontFace& primaryFace,
            const MsdfBakeSettings& settings,
            const MsdfGlyphAllocateFn& allocate);

        /// @brief 指定した文字集合をまとめて焼いてアトラスを作る
        /// @param faceChain フォントの優先順リスト（メトリクスは先頭のものを使う）
        /// @param codePoints 焼く文字（重複していても構わない）
        /// @param settings 解像度・距離場範囲・アトラスサイズ
        static MsdfBakeResult Bake(
            const std::vector<const DirectWriteFontFace*>& faceChain,
            const std::vector<char32_t>& codePoints,
            const MsdfBakeSettings& settings);

        /// @brief アトラスの 1 枚を PNG に書き出す（目視確認用）
        /// @param page 書き出す枚の添字
        static bool SaveAtlasPng(const MsdfBakeResult& result, int page,
            const std::filesystem::path& outPath);

        /// @brief 生の画素バッファ 1 枚ぶんを PNG に書き出す（実行時アトラスのダンプ用）
        static bool SaveAtlasPng(const uint8_t* pixels, int width, int height,
            const std::filesystem::path& outPath);
    };
}
