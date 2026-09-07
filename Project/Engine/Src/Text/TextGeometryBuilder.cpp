#include "pch.h"
#include "TextGeometryBuilder.h"

#include "Text/MsdfFont.h"
#include "Text/TextEncoding.h"
#include "Text/TextLineBreak.h"

#include <algorithm>

namespace CoreEngine::TextGeometry
{
    namespace
    {
        constexpr size_t kNoBreakCandidate = (std::numeric_limits<size_t>::max)();

        /// @brief 行の範囲（コードポイント列への添字）と幅（em）
        struct LineRange
        {
            size_t begin = 0;
            size_t end = 0;   ///< 半開区間
            float  width = 0.0f;
        };

        /// @brief 折り返し位置を決めて行に分ける
        /// @param codePoints 文字列
        /// @param glyphs 各文字のグリフ情報（codePoints と同じ長さ）
        /// @param wrapWidthEm 折り返し幅（em）。0 なら改行文字だけで分ける
        void BuildLines(const std::vector<char32_t>& codePoints,
            const std::vector<MsdfGlyph>& glyphs,
            float wrapWidthEm,
            std::vector<LineRange>& outLines)
        {
            outLines.clear();

            const size_t count = codePoints.size();
            const bool wrapping = wrapWidthEm > 0.0f;

            size_t lineBegin = 0;
            float lineWidth = 0.0f;
            // 「ここで折ってよい」位置と、そこまでの幅
            size_t breakCandidate = kNoBreakCandidate;
            float breakCandidateWidth = 0.0f;

            for (size_t i = 0; i < count; ++i) {
                const char32_t codePoint = codePoints[i];
                if (codePoint == U'\r') { continue; }

                if (codePoint == U'\n') {
                    outLines.push_back({ lineBegin, i, lineWidth });
                    lineBegin = i + 1;
                    lineWidth = 0.0f;
                    breakCandidate = kNoBreakCandidate;
                    continue;
                }

                // この文字の直前で折れるなら候補として覚えておく。
                // 禁則（行頭に句読点・行末に開き括弧）はここで弾かれる
                if (wrapping && i > lineBegin
                    && TextLineBreak::CanBreakBetween(codePoints[i - 1], codePoint)) {
                    breakCandidate = i;
                    breakCandidateWidth = lineWidth;
                }

                const float advance = glyphs[i].advance;

                if (wrapping && i > lineBegin && lineWidth + advance > wrapWidthEm) {
                    // 候補が無ければその場で強制的に折る
                    // （欧文の長い単語や、禁則で候補が潰れた場合）
                    const bool hasCandidate =
                        breakCandidate != kNoBreakCandidate && breakCandidate > lineBegin;
                    const size_t breakAt = hasCandidate ? breakCandidate : i;
                    const float width = hasCandidate ? breakCandidateWidth : lineWidth;

                    outLines.push_back({ lineBegin, breakAt, width });

                    lineBegin = breakAt;
                    // 行頭に残る空白は詰める
                    while (lineBegin < count && codePoints[lineBegin] == U' ') {
                        ++lineBegin;
                    }

                    lineWidth = 0.0f;
                    breakCandidate = kNoBreakCandidate;

                    // 折った位置から走査し直す（lineBegin は必ず前進するので止まらない）
                    i = lineBegin - 1;
                    continue;
                }

                lineWidth += advance;
            }

            if (lineBegin < count || outLines.empty()) {
                outLines.push_back({ lineBegin, count, lineWidth });
            }
        }
    }

    BuildResult Build(MsdfFont& font,
        const std::string& textUtf8,
        const BuildParams& params,
        std::vector<TextGlyphVertex>& outVertices)
    {
        outVertices.clear();

        BuildResult result{};
        if (!font.IsValid()) { return result; }

        // 空文字列で抜ける場合もここで拾っておく。
        // 据え置くと呼び出し側の「世代が変わったら組み直す」判定が毎フレーム真になり、
        // 文字が無いのに再構築が回り続ける
        result.glyphGeneration = font.GetGlyphGeneration();

        const std::vector<char32_t> codePoints = Utf8ToUtf32(textUtf8);
        if (codePoints.empty()) { return result; }

        // アトラスに無い文字は裏で焼いてもらう。焼き上がるとフォント側の
        // グリフ世代が進み、呼び出し側がそれを見て再度ここへ来る
        font.RequestGlyphs(codePoints);
        result.glyphGeneration = font.GetGlyphGeneration();

        // グリフ情報は先にまとめて引く。1 文字ずつ引くとフォント側のロックを
        // 文字数ぶん取ることになり、ワーカーのベイクと競合しやすい。
        // アトラスに無い文字は .notdef（□）へ倒れる。
        // ここで捨てると「文字が黙って消える」ことになり、不具合に気付けない
        std::vector<MsdfGlyph> glyphs;
        glyphs.reserve(codePoints.size());
        for (char32_t codePoint : codePoints) {
            glyphs.push_back(font.ResolveGlyph(codePoint));
        }

        const MsdfFontMetrics& metrics = font.GetMetrics();
        const float lineAdvance = metrics.lineHeight * params.lineSpacing;

        // ── ①行に分ける（折り返し + 禁則処理）──────────────────
        std::vector<LineRange> lines;
        BuildLines(codePoints, glyphs, params.wrapWidthEm, lines);
        result.lineCount = static_cast<uint32_t>(lines.size());

        float maxWidth = 0.0f;
        size_t drawableGlyphCount = 0;
        for (const LineRange& line : lines) {
            maxWidth = (std::max)(maxWidth, line.width);
            for (size_t i = line.begin; i < line.end; ++i) {
                if (glyphs[i].hasBitmap) { ++drawableGlyphCount; }
            }
        }

        const float totalHeight =
            static_cast<float>(lines.size() - 1) * lineAdvance
            + (metrics.ascender - metrics.descender);

        // 折り返しているなら、囲み矩形は指定幅そのものとして扱う
        // （中央寄せ等で「指定した箱」を基準にできるようにする）
        result.measuredSizeEm = {
            (params.wrapWidthEm > 0.0f) ? params.wrapWidthEm : maxWidth,
            totalHeight
        };

        // 文字を流し込む枠。自動調整なら文字列を囲む最小の大きさ
        const Vector2 fieldEm = params.autoFitField ? result.measuredSizeEm : params.fieldEm;
        result.fieldEm = fieldEm;

        result.requestedGlyphCount = drawableGlyphCount;
        if (drawableGlyphCount == 0) { return result; }

        // 共有インデックスバッファの長さが上限。超えた分は切り捨てる
        if (drawableGlyphCount > params.maxGlyphs) {
            drawableGlyphCount = params.maxGlyphs;
            result.truncated = true;
        }

        // ── ②クワッドを組む ──────────────────────────────────────
        // 全て em 単位。フォントサイズ・位置・回転は描画時にまとめて掛ける。
        // 原点はフィールドの左上（ピボット基準）
        const float originX = -params.pivot.x * fieldEm.x;

        // 縦揃え：文字列全体の高さとフィールドの高さの差を配る
        const float verticalSlack = fieldEm.y - totalHeight;
        const float alignOffsetY =
            (params.alignV == TextAlignV::Middle) ? verticalSlack * 0.5f :
            (params.alignV == TextAlignV::Bottom) ? verticalSlack : 0.0f;

        const float originY = -params.pivot.y * fieldEm.y + alignOffsetY;

        // 横揃え：行ごとに幅が違うので、行単位で書き出し位置をずらす
        const auto alignOffsetX = [&params, &fieldEm](float lineWidth) {
            switch (params.alignH) {
            case TextAlignH::Center: return (fieldEm.x - lineWidth) * 0.5f;
            case TextAlignH::Right:  return  fieldEm.x - lineWidth;
            default:                 return 0.0f;
            }
            };

        outVertices.reserve(drawableGlyphCount * 4);

        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const LineRange& line = lines[lineIndex];
            const float baselineY =
                originY + metrics.ascender + static_cast<float>(lineIndex) * lineAdvance;

            float penX = alignOffsetX(line.width);
            for (size_t i = line.begin; i < line.end; ++i) {
                const MsdfGlyph& glyph = glyphs[i];

                if (glyph.hasBitmap && outVertices.size() / 4 < drawableGlyphCount) {
                    // ここは常に Y 下正で組む。フォントの plane 境界は Y 上正なので符号を反転する
                    const float left = originX + penX + glyph.planeLeft;
                    const float right = originX + penX + glyph.planeRight;
                    const float top = baselineY - glyph.planeTop;
                    const float bottom = baselineY - glyph.planeBottom;

                    // texcoord.z にアトラス配列の枚番号を載せる。
                    // 複数枚にまたがる文字列でもドローコールが分かれない
                    const float page = static_cast<float>(glyph.page);

                    outVertices.push_back({ { left,  bottom }, { glyph.uvLeft,  glyph.uvBottom, page } });
                    outVertices.push_back({ { left,  top    }, { glyph.uvLeft,  glyph.uvTop,    page } });
                    outVertices.push_back({ { right, bottom }, { glyph.uvRight, glyph.uvBottom, page } });
                    outVertices.push_back({ { right, top    }, { glyph.uvRight, glyph.uvTop,    page } });
                }

                penX += glyph.advance;
            }
        }

        // ワールド空間は Y 上正。組版は常に Y 下正で行ったので、最後にまとめて反転する。
        // 揃え・ピボット・行送りの計算を空間ごとに分岐させないためにこうしている
        // （分岐させると「3D のときだけ縦揃えが逆」のような取り違えが必ず出る）。
        // 面の向きは変わるが、PSO は両面描画なので描き分けは不要。
        if (!params.yAxisDown) {
            for (TextGlyphVertex& vertex : outVertices) {
                vertex.position.y = -vertex.position.y;
            }
        }

        result.glyphCount = outVertices.size() / 4;
        return result;
    }
}
