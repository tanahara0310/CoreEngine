#include "pch.h"
#include "UIText.h"

#include "Graphics/Render/RenderManager.h"
#include "EngineSystem/EngineSystem.h"
#include "Text/MsdfFont.h"
#include "Text/TextEncoding.h"
#include "Text/TextLineBreak.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <limits>

#ifdef USE_IMGUI
#include "Editor/ImGui/Wrappers/ImGuiInput.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#include <imgui.h>
#include <numbers>
#endif

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        constexpr size_t kNoBreakCandidate = (std::numeric_limits<size_t>::max)();

        /// 距離場は輪郭の外側 pxRange/2 までしか情報を持たない。
        /// 端ぎりぎりは値が飽和しているので、少し内側を上限にする
        constexpr float kMaxOutlineSd = 0.45f;
    }

    void UIText::Initialize(MsdfFont* font, const std::string& textUtf8, const std::string& name)
    {
        if (!name.empty()) {
            SetName(name);
        }

        auto* engine = GetEngineSystem();
        auto* renderManager = engine->GetService<RenderManager>();
        renderer_ = dynamic_cast<TextRenderer*>(renderManager->GetRenderer(RenderPassType::UIText));

        font_ = font;
        textUtf8_ = textUtf8;

        // 文字の左上を基準にしたほうが HUD の配置は考えやすい
        layout_.pivot = { 0.0f, 0.0f };

        RebuildGeometry();
    }

    void UIText::SetText(const std::string& textUtf8)
    {
        if (textUtf8_ == textUtf8) { return; }
        textUtf8_ = textUtf8;
        geometryDirty_ = true;
    }

    void UIText::SetFontSize(float pixelSize)
    {
        if (fontSize_ == pixelSize) { return; }
        fontSize_ = pixelSize;

        // 折り返し幅は px 指定なので、フォントサイズが変わると折り位置が変わる。
        // 折り返しを使っていなければ頂点は em 単位のまま使い回せる
        if (wrapWidthPx_ > 0.0f) {
            geometryDirty_ = true;
        }
    }

    void UIText::SetWrapWidth(float pixelWidth)
    {
        if (wrapWidthPx_ == pixelWidth) { return; }
        wrapWidthPx_ = pixelWidth;
        geometryDirty_ = true;
    }

    void UIText::SetLineSpacing(float scale)
    {
        if (lineSpacing_ == scale) { return; }
        lineSpacing_ = scale;
        geometryDirty_ = true;
    }

    void UIText::SetPivot(const Vector2& pivot)
    {
        if (layout_.pivot.x == pivot.x && layout_.pivot.y == pivot.y) { return; }
        layout_.pivot = pivot;
        geometryDirty_ = true;
    }

    void UIText::SetOutline(const Vector4& color, float widthEm)
    {
        style_.outlineColor = color;
        style_.outlineWidthEm = (std::max)(widthEm, 0.0f);
    }

    float UIText::GetMaxOutlineWidth() const
    {
        if (!font_) { return 0.0f; }
        const float pxRange = font_->GetPxRange();
        if (pxRange <= 0.0f) { return 0.0f; }

        // em → 距離場の値。この逆数に上限値を掛けたものが表現できる最大幅
        const float sdUnitsPerEm = static_cast<float>(font_->GetGlyphPixelSize()) / pxRange;
        return (sdUnitsPerEm > 0.0f) ? (kMaxOutlineSd / sdUnitsPerEm) : 0.0f;
    }

    void UIText::BuildLines(const std::vector<char32_t>& codePoints,
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

    void UIText::RebuildGeometry()
    {
        geometryDirty_ = false;
        glyphVertices_.clear();
        measuredSizeEm_ = { 0.0f, 0.0f };
        lineCount_ = 0;

        if (!font_ || !font_->IsValid()) { return; }

        const std::vector<char32_t> codePoints = Utf8ToUtf32(textUtf8_);
        if (codePoints.empty()) { return; }

        // アトラスに無い文字は裏で焼いてもらう。焼き上がるとフォント側の
        // グリフ世代が進み、Draw がそれを見て再度ここへ来る
        font_->RequestGlyphs(codePoints);
        lastGlyphGeneration_ = font_->GetGlyphGeneration();

        // グリフ情報は先にまとめて引く。1 文字ずつ引くとフォント側のロックを
        // 文字数ぶん取ることになり、ワーカーのベイクと競合しやすい。
        // アトラスに無い文字は .notdef（□）へ倒れる。
        // ここで捨てると「文字が黙って消える」ことになり、不具合に気付けない
        std::vector<MsdfGlyph> glyphs;
        glyphs.reserve(codePoints.size());
        for (char32_t codePoint : codePoints) {
            glyphs.push_back(font_->ResolveGlyph(codePoint));
        }

        const MsdfFontMetrics& metrics = font_->GetMetrics();
        const float lineAdvance = metrics.lineHeight * lineSpacing_;

        // ── ①行に分ける（折り返し + 禁則処理）──────────────────
        const float wrapWidthEm = (wrapWidthPx_ > 0.0f && fontSize_ > 0.0f)
            ? wrapWidthPx_ / fontSize_
            : 0.0f;

        std::vector<LineRange> lines;
        BuildLines(codePoints, glyphs, wrapWidthEm, lines);
        lineCount_ = static_cast<uint32_t>(lines.size());

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
        measuredSizeEm_ = { (wrapWidthEm > 0.0f) ? wrapWidthEm : maxWidth, totalHeight };

        if (drawableGlyphCount == 0) { return; }

        // 共有インデックスバッファの長さが上限。超えた分は切り捨てる
        if (drawableGlyphCount > TextRenderer::kMaxGlyphsPerText) {
            if (!glyphLimitWarned_) {
                glyphLimitWarned_ = true;
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
                    "UIText '{}': グリフ数が上限 {} を超えたため切り詰めました（要求 {}）",
                    GetName(), TextRenderer::kMaxGlyphsPerText, drawableGlyphCount);
            }
            drawableGlyphCount = TextRenderer::kMaxGlyphsPerText;
        }

        // ── ②クワッドを組む ──────────────────────────────────────
        // 全て em 単位。フォントサイズ・位置・回転は描画時にまとめて掛ける
        const float originX = -layout_.pivot.x * measuredSizeEm_.x;
        const float originY = -layout_.pivot.y * totalHeight;

        glyphVertices_.reserve(drawableGlyphCount * 4);

        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
            const LineRange& line = lines[lineIndex];
            const float baselineY =
                originY + metrics.ascender + static_cast<float>(lineIndex) * lineAdvance;

            float penX = 0.0f;
            for (size_t i = line.begin; i < line.end; ++i) {
                const MsdfGlyph& glyph = glyphs[i];

                if (glyph.hasBitmap && glyphVertices_.size() / 4 < drawableGlyphCount) {
                    // UI 座標系は Y 下正。フォントの plane 境界は Y 上正なので符号を反転する
                    const float left = originX + penX + glyph.planeLeft;
                    const float right = originX + penX + glyph.planeRight;
                    const float top = baselineY - glyph.planeTop;
                    const float bottom = baselineY - glyph.planeBottom;

                    // texcoord.z にアトラス配列の枚番号を載せる。
                    // 複数枚にまたがる文字列でもドローコールが分かれない
                    const float page = static_cast<float>(glyph.page);

                    glyphVertices_.push_back({ { left,  bottom }, { glyph.uvLeft,  glyph.uvBottom, page } });
                    glyphVertices_.push_back({ { left,  top    }, { glyph.uvLeft,  glyph.uvTop,    page } });
                    glyphVertices_.push_back({ { right, bottom }, { glyph.uvRight, glyph.uvBottom, page } });
                    glyphVertices_.push_back({ { right, top    }, { glyph.uvRight, glyph.uvTop,    page } });
                }

                penX += glyph.advance;
            }
        }
    }

    void UIText::Draw(const Camera* /*camera*/)
    {
        // RenderGraph を経由しない直接呼び出し（レガシー経路）
        DrawViewInfo view{};
        view.cmdList = renderer_ ? renderer_->GetGraphicsCore()->GetCommandList() : nullptr;
        Draw(view);
    }

    void UIText::Draw(const DrawViewInfo& view)
    {
        if (!IsActive() || !renderer_ || !font_ || !font_->IsValid()) { return; }
        if (!view.cmdList) { return; }

        // 実行時ベイクで新しいグリフが増えていたら組み直す。
        // これが □ から本来の字へ差し替わる瞬間
        if (font_->GetGlyphGeneration() != lastGlyphGeneration_) {
            geometryDirty_ = true;
        }
        if (geometryDirty_) { RebuildGeometry(); }
        if (glyphVertices_.empty()) { return; }

        const Vector2 screenPos = layout_.CalculateScreenPosition(renderer_->GetScreenSize());

        const Vector3 position = { screenPos.x, screenPos.y, 0.0f };
        // 頂点が em 単位なので、フォントサイズがそのままスケールになる
        const Vector3 scale = { fontSize_, fontSize_, 1.0f };
        const Vector3 rotation = { 0.0f, 0.0f, layout_.rotation };
        const Matrix4x4 world = Matrix::MakeAffine(scale, rotation, position);

        // ここではドローコールを出さず、レンダラーのバッチへ積むだけ。
        // 実際の描画は EndPass（またはフォント切り替え）で 1 本にまとめて出る
        renderer_->Submit(font_, glyphVertices_.data(), glyphVertices_.size(), world, style_);
    }

#ifdef USE_IMGUI
    int UIText::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const
    {
        if (maxTabs < 2) { return 0; }
        outTabs[0] = { "object_data.png", "レイアウト", {0.96f,0.65f,0.14f,1.0f}, {0.96f,0.65f,0.14f,0.25f} };
        outTabs[1] = { "material.png",    "テキスト",   {0.30f,0.70f,0.90f,1.0f}, {0.30f,0.70f,0.90f,0.25f} };
        return 2;
    }

    bool UIText::DrawInspectorTabContent(int tabIndex)
    {
        bool changed = false;

        switch (tabIndex)
        {
            // ── 0: レイアウト ──────────────────────────────────────
        case 0:
        {
            UI::SectionHeader("アンカー");
            {
                static const char* kAnchorNames[] = {
                    "TopLeft",    "TopCenter",    "TopRight",
                    "MiddleLeft", "Center",       "MiddleRight",
                    "BottomLeft", "BottomCenter", "BottomRight",
                };
                int anchorIdx = static_cast<int>(layout_.anchor);
                if (ImGui::Combo("##anchor", &anchorIdx, kAnchorNames, 9)) {
                    layout_.anchor = static_cast<UIAnchor>(anchorIdx);
                    changed = true;
                }
            }

            UI::SectionHeader("AnchoredPosition");
            if (UI::DragVec2("##anchoredPos", layout_.anchoredPos, 1.0f)) {
                changed = true;
            }

            UI::SectionHeader("Pivot");
            {
                Vector2 pivotTmp = layout_.pivot;
                if (UI::DragVec2("##pivot", pivotTmp, 0.01f, 0.0f, 1.0f)) {
                    SetPivot(pivotTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("折り返し幅");
            {
                float wrapTmp = wrapWidthPx_;
                if (UI::DragFloat("px（0 で無効）##wrap", wrapTmp, 1.0f, 0.0f, 4096.0f)) {
                    SetWrapWidth(wrapTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("回転");
            {
                float rotDeg = layout_.rotation * (180.0f / static_cast<float>(std::numbers::pi));
                if (UI::DragFloat("度##rot", rotDeg, 0.5f, -360.0f, 360.0f)) {
                    layout_.rotation = rotDeg * (static_cast<float>(std::numbers::pi) / 180.0f);
                    changed = true;
                }
            }

            UI::SectionHeader("描画順序");
            {
                int sortTmp = layout_.sortOrder;
                if (ImGui::DragInt("Sort Order##so", &sortTmp, 1, -9999, 9999)) {
                    SetSortOrder(sortTmp);
                    changed = true;
                }
            }
            break;
        }

        // ── 1: テキスト ────────────────────────────────────────
        case 1:
        {
            UI::SectionHeader("フォントサイズ");
            {
                float sizeTmp = fontSize_;
                if (UI::DragFloat("px##fontSize", sizeTmp, 0.5f, 1.0f, 512.0f)) {
                    // 頂点は em 単位なので、折り返しが無ければ再構築不要。
                    // ここが MSDF の効きどころで、何倍にしても輪郭は鋭いまま
                    SetFontSize(sizeTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("行間");
            {
                float spacingTmp = lineSpacing_;
                if (UI::DragFloat("倍##lineSpacing", spacingTmp, 0.01f, 0.1f, 4.0f)) {
                    SetLineSpacing(spacingTmp);
                    changed = true;
                }
            }

            UI::SectionHeader("カラー");
            {
                Vector4 color = style_.color;
                if (UI::ColorEdit("##color", color)) {
                    style_.color = color;
                    changed = true;
                }
            }

            UI::SectionHeader("縁取り");
            {
                Vector4 outlineColor = style_.outlineColor;
                float outlineWidth = style_.outlineWidthEm;
                const float maxWidth = GetMaxOutlineWidth();

                bool outlineChanged = UI::ColorEdit("色##outline", outlineColor);
                outlineChanged |= UI::DragFloat("太さ(em)##outline", outlineWidth,
                    0.001f, 0.0f, maxWidth);
                if (outlineChanged) {
                    SetOutline(outlineColor, outlineWidth);
                    changed = true;
                }
                ImGui::TextDisabled("上限 %.3f em（pxRange を上げると広がる）", maxWidth);
            }

            UI::SectionHeader("太さ調整");
            {
                float weight = style_.weightEm;
                if (UI::DragFloat("em##weight", weight, 0.001f, -0.05f, 0.05f)) {
                    style_.weightEm = weight;
                    changed = true;
                }
            }

            UI::SectionHeader("情報");
            {
                const Vector2 measured = GetMeasuredSize();
                ImGui::Text("文字列: %s", textUtf8_.c_str());
                ImGui::Text("実寸: %.1f x %.1f px / %u 行", measured.x, measured.y, lineCount_);
                ImGui::Text("グリフ数: %u / %u", GetGlyphCount(), TextRenderer::kMaxGlyphsPerText);
                if (renderer_) {
                    ImGui::Text("テキスト全体: %u ドローコール / %u グリフ",
                        renderer_->GetLastFrameDrawCallCount(),
                        renderer_->GetLastFrameGlyphCount());
                }
                if (font_) {
                    const Vector2 atlas = font_->GetAtlasSize();
                    ImGui::Text("アトラス: %.0f x %.0f x%d枚 / pxRange %.1f",
                        atlas.x, atlas.y, font_->GetPageCount(), font_->GetPxRange());
                }
            }
            break;
        }

        default: break;
        }

        return changed;
    }
#endif // USE_IMGUI
}
