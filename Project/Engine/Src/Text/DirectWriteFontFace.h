#pragma once

#include "Text/MsdfFontTypes.h"

#include <cstdint>
#include <string>
#include <vector>
#include <wrl.h>

// 前方宣言（msdfgen のヘッダをこちらへ波及させない）
namespace msdfgen { class Shape; }

struct IDWriteFactory;
struct IDWriteFontFace;

namespace CoreEngine
{
    /// @brief DirectWrite でフォントを読み、グリフのアウトラインとメトリクスを取り出す
    /// @details
    ///  MSDF 生成の「①フォントを解析する」工程だけを担う。
    ///  msdfgen 側（②③）はフォント形式を知らないので、ここが唯一の入口になる。
    ///
    ///  FreeType ではなく DirectWrite を使う理由:
    ///   - Windows SDK に含まれるので外部依存が増えない（dwrite.lib を 1 行リンクするだけ）
    ///   - システムフォントを名前で引ける（フォントファイルを同梱しなくてよい）
    ///   - .ttc / cmap format 12 / GSUB・GPOS など和文に必要な機能を全て内蔵している
    ///
    /// @note 取り出すアウトラインはヒンティング前の生の輪郭。MSDF に必要なのはこちら。
    class DirectWriteFontFace
    {
    public:
        DirectWriteFontFace();
        ~DirectWriteFontFace();

        DirectWriteFontFace(const DirectWriteFontFace&) = delete;
        DirectWriteFontFace& operator=(const DirectWriteFontFace&) = delete;

        /// @brief フォントファイルから読み込む（インストール不要）
        /// @param filePath .ttf / .otf / .ttc のパス
        /// @param faceIndex .ttc 内のフェイス番号（単一フォントは 0）
        bool LoadFromFile(const std::wstring& filePath, uint32_t faceIndex = 0);

        /// @brief インストール済みシステムフォントから読み込む
        /// @param familyName ファミリ名（例: L"Yu Gothic UI"）
        bool LoadFromSystem(const std::wstring& familyName);

        /// @brief 候補を順に試して最初に見つかったシステムフォントを読み込む
        /// @param familyNames 優先順のファミリ名リスト
        /// @return 採用したファミリ名（全滅なら空）
        std::wstring LoadFromSystemPreferred(const std::vector<std::wstring>& familyNames);

        /// @brief 読み込み済みか
        bool IsValid() const { return fontFace_ != nullptr; }

        /// @brief コードポイントからグリフ ID を引く
        /// @return フォントに収録が無ければ 0（.notdef）
        uint16_t GetGlyphIndex(char32_t codePoint) const;

        /// @brief グリフのアウトラインを msdfgen::Shape として組み立てる
        /// @param glyphIndex GetGlyphIndex() で得たグリフ ID
        /// @param outShape 出力先（呼び出し前に空であること）
        /// @return 成功したら true。空白文字など輪郭を持たないグリフでも true を返す
        ///         （その場合 outShape の contours は空になる）
        /// @note 座標系はフォント設計空間（ベースライン原点・**Y 上正**・em 単位）へ
        ///       正規化して返す。DirectWrite が返すのは Y 下正なので内部で反転している。
        bool BuildShape(uint16_t glyphIndex, msdfgen::Shape& outShape) const;

        /// @brief グリフの字送り幅（em 単位）
        float GetAdvance(uint16_t glyphIndex) const;

        /// @brief フォント全体の縦組みメトリクス（em 単位）
        const MsdfFontMetrics& GetMetrics() const { return metrics_; }

        /// @brief 読み込み元の表示名（ログ用）
        const std::wstring& GetDisplayName() const { return displayName_; }

    private:
        /// @brief IDWriteFactory を用意する（プロセス共有）
        static IDWriteFactory* AcquireFactory();

        /// @brief fontFace_ 設定後にメトリクスを取り込む
        void CacheMetrics();

        Microsoft::WRL::ComPtr<IDWriteFontFace> fontFace_;
        MsdfFontMetrics metrics_{};
        float designUnitsPerEm_ = 0.0f;
        std::wstring displayName_;
    };
}
