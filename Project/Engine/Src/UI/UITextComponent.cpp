#include "pch.h"
#include "UI/UITextComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/ComponentService.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderManager.h"
#include "Text/FontManager.h"
#include "Text/MsdfFont.h"
#include "Text/TextGeometryBuilder.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIElement.h"
#include "Utility/Algorithm/AssignIfChanged.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

namespace
{
    /// @brief フォントの欄に出す候補（登録名とフォントフォルダのファイル名）
    std::vector<std::string> ListFontNames(const void* instance)
    {
        const auto& text = *static_cast<const CoreEngine::UITextComponent*>(instance);
        const CoreEngine::FontManager* const fonts = CoreEngine::FindService<CoreEngine::FontManager>(text);
        return fonts ? fonts->GetSelectableFontNames() : std::vector<std::string>{};
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::UITextComponent, "UI テキスト")
    REFLECT_ACCESSOR("text", "テキスト", GetText, SetText,
        p.flags = ::CoreEngine::Reflection::PropertyFlags::Multiline)
    REFLECT_ACCESSOR("font", "フォント", GetFontName, SetFontByName,
        p.stringChoices = &ListFontNames,
        p.tooltip = "一覧に無いフォントは、フォントフォルダのファイル名かインストール済みのフォント名を入力する")
    REFLECT_ACCESSOR("fontSize", "フォントサイズ", GetFontSize, SetFontSize,
        p.range = Range(1.0f, 512.0f, 0.5f))
    REFLECT_ACCESSOR("lineSpacing", "行間", GetLineSpacing, SetLineSpacing,
        p.range = Range(0.1f, 4.0f, 0.01f))
    REFLECT_ACCESSOR("fieldAutoFit", "枠を文字に合わせる", IsFieldAutoFit, SetFieldAutoFit,
        p.tooltip = "UI トランスフォームの大きさを文字列に合わせる。大きさを変えると切れ、その枠の中で折り返す")
    REFLECT_ACCESSOR("wrapWidth", "折り返し幅", GetWrapWidth, SetWrapWidth,
        p.range = Range(0.0f, 4096.0f, 1.0f),
        p.tooltip = "0 で折り返さない。枠を文字に合わせていないときは枠の幅で折り返す")
    REFLECT_ENUM_ACCESSOR("alignH", "横揃え", GetAlignH, SetAlignH, kAlignHNames)
    REFLECT_ENUM_ACCESSOR("alignV", "縦揃え", GetAlignV, SetAlignV, kAlignVNames)
    REFLECT_ACCESSOR("color", "色", GetColor, SetColor,
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_ACCESSOR("outlineColor", "縁取りの色", GetOutlineColor, SetOutlineColor,
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_ACCESSOR("outlineWidth", "縁取りの太さ", GetOutlineWidth, SetOutlineWidth,
        p.range = Range(0.0f, 0.25f, 0.001f),
        p.tooltip = "em 単位。フォントの距離場の幅で上限が決まる")
    REFLECT_ACCESSOR("weight", "太さ調整", GetWeight, SetWeight,
        p.range = Range(-0.05f, 0.05f, 0.001f))
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::UITextComponent)
COMPONENT_REGISTER(CoreEngine::UITextComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    UITextComponent::UITextComponent()
        : fontName_(FontManager::kDefaultFontName)
    {
    }

    UITextComponent::~UITextComponent() = default;

    void UITextComponent::Awake()
    {
        GameObject* const owner = GetOwner();
        if (!owner) { return; }

        // 配置は兄弟の UI トランスフォームから取る。無ければ足す
        rect_ = owner->GetOrAddComponent<RectTransformComponent>();
        if (rect_) {
            rect_->SetSizeDriven(fieldAutoFit_);
        }

        if (auto* const renderManager = FindService<RenderManager>(*this)) {
            renderer_ = renderManager->GetRendererAs<TextRenderer>(RenderPassType::UIText);
        }

        // フォントは直後にコードから名前を変えられることがあるので、最初に描くときに引く
        geometryDirty_ = true;
    }

    void UITextComponent::OnDestroy()
    {
        if (RectTransformComponent* const rect = Sibling<RectTransformComponent>()) {
            rect->SetSizeDriven(false);
        }
    }

    void UITextComponent::ResolveFont()
    {
        FontManager* const fonts = FindService<FontManager>(*this);
        if (!fonts) { return; }

        MsdfFont* const resolved = fonts->AcquireNamed(fontName_);
        fontResolveFailed_ = (resolved == nullptr);
        if (resolved && resolved != font_) {
            font_ = resolved;
            geometryDirty_ = true;
        }
    }

    void UITextComponent::SetFontByName(const std::string& fontName)
    {
        const std::string name = fontName.empty() ? std::string(FontManager::kDefaultFontName) : fontName;
        if (name == fontName_ && font_) { return; }

        // 持ち主が無い（取り付ける前）ときは名前だけ控え、最初に描くときに引く
        fontName_ = name;
        fontResolveFailed_ = false;
        ResolveFont();
    }

    void UITextComponent::SetText(const std::string& textUtf8)
    {
        if (AssignIfChanged(text_, textUtf8)) { geometryDirty_ = true; }
    }

    void UITextComponent::SetFontSize(float pixelSize)
    {
        pixelSize = (std::max)(pixelSize, 0.0f);
        if (fontSize_ == pixelSize) { return; }
        fontSize_ = pixelSize;

        // 折り返し幅も固定した枠も px なので、どちらかを使っていれば折り位置と揃えが変わる
        if (wrapWidth_ > 0.0f || !fieldAutoFit_) {
            geometryDirty_ = true;
        } else {
            // 頂点は em 単位のまま使えるので、枠の大きさだけ合わせ直す
            fitDirty_ = true;
        }
    }

    void UITextComponent::SetLineSpacing(float scale)
    {
        if (AssignIfChanged(lineSpacing_, scale)) { geometryDirty_ = true; }
    }

    void UITextComponent::SetFieldAutoFit(bool enable)
    {
        if (fieldAutoFit_ == enable) { return; }
        fieldAutoFit_ = enable;
        hasFitted_ = false;
        geometryDirty_ = true;
        if (RectTransformComponent* const rect = GetRectTransform()) {
            rect->SetSizeDriven(enable);
        }
    }

    void UITextComponent::SetWrapWidth(float pixelWidth)
    {
        pixelWidth = (std::max)(pixelWidth, 0.0f);
        if (AssignIfChanged(wrapWidth_, pixelWidth)) { geometryDirty_ = true; }
    }

    void UITextComponent::SetAlignH(TextAlignH alignment)
    {
        if (AssignIfChanged(alignH_, alignment)) { geometryDirty_ = true; }
    }

    void UITextComponent::SetAlignV(TextAlignV alignment)
    {
        if (AssignIfChanged(alignV_, alignment)) { geometryDirty_ = true; }
    }

    void UITextComponent::SetAlign(TextAlignH horizontal, TextAlignV vertical)
    {
        SetAlignH(horizontal);
        SetAlignV(vertical);
    }

    void UITextComponent::SetOutlineWidth(float widthEm)
    {
        style_.outlineWidthEm = (std::max)(widthEm, 0.0f);
    }

    void UITextComponent::SetOutline(const Vector4& color, float widthEm)
    {
        SetOutlineColor(color);
        SetOutlineWidth(widthEm);
    }

    Vector2 UITextComponent::GetMeasuredSize() const
    {
        return { measuredSizeEm_.x * fontSize_, measuredSizeEm_.y * fontSize_ };
    }

    Vector2 UITextComponent::GetPreferredSize()
    {
        if (!font_ && !fontResolveFailed_) {
            ResolveFont();
        }
        RectTransformComponent* const rect = GetRectTransform();
        if (rect && font_ && font_->IsValid()) {
            const Vector2 canvasSize = renderer_ ? renderer_->GetScreenSize() : rect->FindCanvasSize();
            UpdateGeometry(*rect, canvasSize);
        }
        return GetMeasuredSize();
    }

    bool UITextComponent::RequiresComponent(const IComponent& other) const
    {
        return IsAnyOf<RectTransformComponent>(other);
    }

    RectTransformComponent* UITextComponent::GetRectTransform() const
    {
        return CachedSibling(rect_);
    }

    void UITextComponent::UpdateGeometry(RectTransformComponent& rect, const Vector2& canvasSize)
    {
        // 文字列に合わせた後で大きさを変えられたら、その大きさの枠に固定する
        if (fieldAutoFit_ && hasFitted_) {
            const Vector2 size = rect.GetSize();
            if (size.x != fittedSize_.x || size.y != fittedSize_.y) {
                fieldAutoFit_ = false;
                hasFitted_ = false;
                geometryDirty_ = true;
                rect.SetSizeDriven(false);
            }
        }

        // 基準点か枠が変わった・実行時ベイクでグリフが増えたら組み直す
        if (rect.GetShapeRevision() != builtShapeRevision_ ||
            font_->GetGlyphGeneration() != builtGlyphGeneration_) {
            geometryDirty_ = true;
        }

        // アンカーが開いている枠は、親の大きさが変わっても組み直す
        if (!fieldAutoFit_ && (rect.GetLayout().IsStretchedX() || rect.GetLayout().IsStretchedY())) {
            const Vector2 fieldSize = rect.GetRectSize(canvasSize);
            if (fieldSize.x != builtFieldSize_.x || fieldSize.y != builtFieldSize_.y) {
                geometryDirty_ = true;
            }
        }

        if (geometryDirty_) {
            RebuildGeometry(rect, canvasSize);
        } else if (fitDirty_) {
            FitRect(rect);
        }
    }

    void UITextComponent::RebuildGeometry(RectTransformComponent& rect, const Vector2& canvasSize)
    {
        geometryDirty_ = false;
        fitDirty_ = false;
        glyphVertices_.clear();
        measuredSizeEm_ = { 0.0f, 0.0f };
        lineCount_ = 0;

        if (!font_ || !font_->IsValid()) { return; }

        const Vector2 fieldSize = rect.GetRectSize(canvasSize);
        builtFieldSize_ = fieldSize;
        const TextGeometry::BuildParams params = MakeBuildParams(rect, fieldSize);

        const TextGeometry::BuildResult result = TextGeometry::Build(*font_, text_, params, glyphVertices_);
        measuredSizeEm_ = result.measuredSizeEm;
        lineCount_ = result.lineCount;
        builtGlyphGeneration_ = result.glyphGeneration;

        if (fieldAutoFit_) {
            FitRect(rect);
        }
        builtShapeRevision_ = rect.GetShapeRevision();

        if (result.truncated && !glyphLimitWarned_) {
            glyphLimitWarned_ = true;
            const GameObject* const owner = GetOwner();
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
                "UIText '{}': グリフ数が上限 {} を超えたため切り詰めました（要求 {}）",
                owner ? owner->GetName() : std::string{}, TextRenderer::kMaxGlyphsPerText,
                result.requestedGlyphCount);
        }
    }

    TextGeometry::BuildParams UITextComponent::MakeBuildParams(const RectTransformComponent& rect,
                                                               const Vector2& fieldSize) const
    {
        // 折り返し幅と枠は px なので em へ直す。枠を固定しているならその幅で折る
        const float wrapWidth = fieldAutoFit_ ? wrapWidth_ : fieldSize.x;

        TextGeometry::BuildParams params{};
        params.lineSpacing = lineSpacing_;
        params.wrapWidthEm = (wrapWidth > 0.0f && fontSize_ > 0.0f) ? wrapWidth / fontSize_ : 0.0f;
        params.autoFitField = fieldAutoFit_;
        params.fieldEm = (fieldAutoFit_ || fontSize_ <= 0.0f)
            ? Vector2{ 0.0f, 0.0f }
            : Vector2{ fieldSize.x / fontSize_, fieldSize.y / fontSize_ };
        params.alignH = alignH_;
        params.alignV = alignV_;
        params.pivot = rect.GetPivot();
        // UI の画面座標は Y 下正
        params.yAxisDown = true;
        params.maxGlyphs = TextRenderer::kMaxGlyphsPerText;
        return params;
    }

    bool UITextComponent::MeasureCarets(const std::string& textUtf8, UITextCaretLayout& outLayout)
    {
        outLayout = {};
        if (!font_ && !fontResolveFailed_) {
            ResolveFont();
        }
        RectTransformComponent* const rect = GetRectTransform();
        if (!rect || !font_ || !font_->IsValid()) {
            return false;
        }

        const Vector2 canvasSize = renderer_ ? renderer_->GetScreenSize() : rect->FindCanvasSize();
        const TextGeometry::BuildParams params = MakeBuildParams(*rect, rect->GetRectSize(canvasSize));
        TextGeometry::CaretLayout layout;
        TextGeometry::LayoutCarets(*font_, textUtf8, params, layout);

        outLayout.stops.reserve(layout.stops.size());
        for (const TextGeometry::CaretStop& stop : layout.stops) {
            outLayout.stops.push_back({ { stop.x * fontSize_, stop.top * fontSize_ }, stop.line });
        }
        outLayout.lineHeight = layout.lineHeight * fontSize_;
        outLayout.lineAdvance = layout.lineAdvance * fontSize_;
        outLayout.lineCount = layout.lineCount;
        return true;
    }

    void UITextComponent::FitRect(RectTransformComponent& rect)
    {
        fitDirty_ = false;

        // 行が 1 つも無い（空文字列）ときは大きさを 0 へ潰さない。潰すと Canvas で選び直せなくなる。
        // アンカーが開いている枠は文字に合わせない
        const bool stretched = rect.GetLayout().IsStretchedX() || rect.GetLayout().IsStretchedY();
        if (lineCount_ > 0 && !stretched) {
            rect.SetSize(GetMeasuredSize());
        }
        fittedSize_ = rect.GetSize();
        hasFitted_ = true;
        builtShapeRevision_ = rect.GetShapeRevision();
    }

    void UITextComponent::Render(const DrawViewInfo& view)
    {
        const GameObject* const owner = GetOwner();
        if (!owner || !owner->IsActive()) { return; }
        if (!font_ && !fontResolveFailed_) {
            ResolveFont();
        }
        if (!view.cmdList || !renderer_ || !font_ || !font_->IsValid()) { return; }

        RectTransformComponent* const rect = GetRectTransform();
        if (!rect || !rect->IsActiveInHierarchy()) { return; }

        const Vector2 canvasSize = renderer_->GetScreenSize();
        UpdateGeometry(*rect, canvasSize);
        if (glyphVertices_.empty()) { return; }

        // 頂点が em 単位なので、フォントサイズがそのまま拡大率になる
        const Matrix4x4 world = rect->BuildDrawMatrix({ fontSize_, fontSize_ }, canvasSize);

        // 親の UI マスクの外は描かない。枠からはみ出す字の分だけ広げて見て、外へ出切っていれば積まない
        UIRect clip;
        const bool clipped = rect->FindClipRect(canvasSize, clip);
        if (clipped && fontSize_ > 0.0f) {
            const Vector2 measured = GetMeasuredSize();
            const Vector2 field = rect->GetRectSize(canvasSize);
            const float overflow = (std::max)({ measured.x - field.x, measured.y - field.y, 0.0f });
            // world の行の長さはフォントサイズ × 親と自分の拡大
            const float emOnCanvas = (std::max)(std::hypot(world.m[0][0], world.m[0][1]),
                                                std::hypot(world.m[1][0], world.m[1][1]));
            const float margin = emOnCanvas * (1.0f + overflow / fontSize_);
            if (!rect->GetCanvasQuad(canvasSize).Bounds().Overlaps(clip, margin)) {
                return;
            }
        }

        // ドローコールは出さず、レンダラーのバッチへ積むだけ
        renderer_->Submit(font_, glyphVertices_.data(), glyphVertices_.size(), world, style_,
            clipped ? &clip : nullptr);
    }
}
