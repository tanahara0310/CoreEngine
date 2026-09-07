#pragma once

#include "Text/MsdfFontTypes.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;

    /// @brief テキストフィールド内での横方向の揃え
    enum class TextAlignH : uint8_t { Left, Center, Right };

    /// @brief テキストフィールド内での縦方向の揃え
    enum class TextAlignV : uint8_t { Top, Middle, Bottom };

    /// @brief 変換前のグリフ頂点
    /// @details
    ///  位置は **em 単位**（フォントサイズ 1.0 のときの大きさ）。
    ///  フォントサイズ・位置・回転はレンダラーへ積むときに掛ける。
    ///  こうしておくと、サイズを変えても頂点は組み直さずに済むし、
    ///  同じ頂点を「スクリーン px」にも「ワールド単位」にも読み替えられる。
    struct TextGlyphVertex
    {
        Vector2 position;
        /// xy = アトラス UV / z = アトラス配列の何枚目か
        Vector3 texcoord;
    };

    /// @brief 文字列からグリフのクワッド列（em 単位）を組み立てる
    /// @details
    ///  折り返し・禁則・整列といった組版はスクリーンにもワールドにも共通なので、
    ///  UIText と Text3DObject はどちらもここを通す。
    ///  出力が em 単位なので、呼び出し側が「1em を何 px と読むか / 何ワールド単位と
    ///  読むか」を決めるだけで同じ組版が両方の空間で使える。
    namespace TextGeometry
    {
        /// @brief 組版の指定（長さは全て em 単位）
        struct BuildParams
        {
            /// @brief 行間の倍率（1.0 でフォント本来の行送り）
            float lineSpacing = 1.0f;

            /// @brief 折り返し幅（em）。0 で折り返し無効（改行文字だけで分ける）
            float wrapWidthEm = 0.0f;

            /// @brief 枠を文字列の大きさへ自動で合わせるか
            /// @details false のときだけ `fieldEm` が使われる
            bool autoFitField = true;

            /// @brief 文字を流し込む枠（em）。`autoFitField == false` のときのみ有効
            Vector2 fieldEm = { 0.0f, 0.0f };

            TextAlignH alignH = TextAlignH::Left;
            TextAlignV alignV = TextAlignV::Top;

            /// @brief 枠の基準点（0,0 = 左上 / 0.5,0.5 = 中央）
            Vector2 pivot = { 0.0f, 0.0f };

            /// @brief Y 軸が下向き正か
            /// @details
            ///  UI のスクリーン座標は Y 下正、3D のワールド座標は Y 上正。
            ///  組版そのものは常に Y 下正で行い、false のときだけ最後に Y を反転する。
            ///  こうしておくと「3D だけ揃えがずれる」類の取り違えが起きない。
            bool yAxisDown = true;

            /// @brief 積めるグリフ数の上限（超えた分は切り捨てる）
            /// @note レンダラー側の共有インデックスバッファの長さで決まる
            size_t maxGlyphs = (std::numeric_limits<size_t>::max)();
        };

        /// @brief 組版の結果（長さは全て em 単位）
        struct BuildResult
        {
            /// @brief 文字列を囲む矩形
            /// @note 折り返しているなら幅は指定幅そのもの（中央寄せ等の基準を安定させるため）
            Vector2 measuredSizeEm = { 0.0f, 0.0f };

            /// @brief 実際に組版へ使った枠
            Vector2 fieldEm = { 0.0f, 0.0f };

            /// @brief 折り返し後の行数
            uint32_t lineCount = 0;

            /// @brief 出力したグリフ数（切り詰め後）
            size_t glyphCount = 0;

            /// @brief 描くべきだったグリフ数（切り詰め前）
            size_t requestedGlyphCount = 0;

            /// @brief `maxGlyphs` で切り詰めたか
            bool truncated = false;

            /// @brief 組んだ時点のフォント側のグリフ世代
            /// @details 実行時ベイクで進む。呼び出し側はこれを覚えておき、
            ///          変化したら組み直すことで □ が本来の字へ差し替わる
            uint32_t glyphGeneration = 0;
        };

        /// @brief 文字列をグリフのクワッド列へ組む
        /// @param font 使用フォント（アトラスに無い文字はここでベイクを要求する）
        /// @param textUtf8 表示文字列（UTF-8。改行 \n に対応）
        /// @param params 組版の指定
        /// @param outVertices 出力先（先頭でクリアされる。4 頂点 = 1 グリフ）
        /// @return 測定結果。文字が 1 つも描けない場合でも寸法と行数は埋まる
        BuildResult Build(MsdfFont& font,
            const std::string& textUtf8,
            const BuildParams& params,
            std::vector<TextGlyphVertex>& outVertices);
    }
}
