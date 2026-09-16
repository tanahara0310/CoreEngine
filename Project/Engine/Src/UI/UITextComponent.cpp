#include "pch.h"
#include "UI/UITextComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/RenderManager.h"
#include "Text/FontManager.h"
#include "Text/MsdfFont.h"
#include "Text/TextGeometryBuilder.h"
#include "UI/RectTransformComponent.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

namespace
{
    /// @brief 距離場で表せる縁取りの上限（距離場の値）
    /// @note MsdfText.PS.hlsl の kMaxOutlineSd と同じ値にしておく
    constexpr float kMaxOutlineSd = 0.375f;

    /// @brief コンポーネントの持ち主からフォントの管理を引く
    CoreEngine::FontManager* FindFontManager(const CoreEngine::IComponent& component)
    {
        const CoreEngine::GameObject* const owner = component.GetOwner();
        CoreEngine::EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
        return engine ? engine->GetService<CoreEngine::FontManager>() : nullptr;
    }

    /// @brief フォントの欄に出す候補（登録名とフォントフォルダのファイル名）
    std::vector<std::string> ListFontNames(const void* instance)
    {
        const auto& text = *static_cast<const CoreEngine::UITextComponent*>(instance);
        const CoreEngine::FontManager* const fonts = FindFontManager(text);
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

        if (EngineSystem* const engine = owner->GetEngineSystem()) {
            if (auto* const renderManager = engine->GetService<RenderManager>()) {
                renderer_ = dynamic_cast<TextRenderer*>(renderManager->GetRenderer(RenderPassType::UIText));
            }
        }

        // フォントは直後にコードから名前を変えられることがあるので、最初に描くときに引く
        geometryDirty_ = true;
    }

    void UITextComponent::ResolveFont()
    {
        FontManager* const fonts = FindFontManager(*this);
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
        if (text_ == textUtf8) { return; }
        text_ = textUtf8;
        geometryDirty_ = true;
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
        if (lineSpacing_ == scale) { return; }
        lineSpacing_ = scale;
        geometryDirty_ = true;
    }

    void UITextComponent::SetFieldAutoFit(bool enable)
    {
        if (fieldAutoFit_ == enable) { return; }
        fieldAutoFit_ = enable;
        hasFitted_ = false;
        geometryDirty_ = true;
    }

    void UITextComponent::SetWrapWidth(float pixelWidth)
    {
        pixelWidth = (std::max)(pixelWidth, 0.0f);
        if (wrapWidth_ == pixelWidth) { return; }
        wrapWidth_ = pixelWidth;
        geometryDirty_ = true;
    }

    void UITextComponent::SetAlignH(TextAlignH alignment)
    {
        if (alignH_ == alignment) { return; }
        alignH_ = alignment;
        geometryDirty_ = true;
    }

    void UITextComponent::SetAlignV(TextAlignV alignment)
    {
        if (alignV_ == alignment) { return; }
        alignV_ = alignment;
        geometryDirty_ = true;
    }

    void UITextComponent::SetAlign(TextAlignH horizontal, TextAlignV vertical)
    {
        SetAlignH(horizontal);
        SetAlignV(vertical);
    }

    void UITextComponent::SetOutlineWidth(float widthEm)
    {
        widthEm = (std::max)(widthEm, 0.0f);
        if (font_) {
            widthEm = (std::min)(widthEm, GetMaxOutlineWidth());
        }
        style_.outlineWidthEm = widthEm;
    }

    void UITextComponent::SetOutline(const Vector4& color, float widthEm)
    {
        SetOutlineColor(color);
        SetOutlineWidth(widthEm);
    }

    float UITextComponent::GetMaxOutlineWidth() const
    {
        if (!font_) { return 0.0f; }
        const float pxRange = font_->GetPxRange();
        if (pxRange <= 0.0f) { return 0.0f; }

        // em を距離場の値へ直す係数の逆数に、上限の値を掛ける
        const float sdUnitsPerEm = static_cast<float>(font_->GetGlyphPixelSize()) / pxRange;
        return (sdUnitsPerEm > 0.0f) ? (kMaxOutlineSd / sdUnitsPerEm) : 0.0f;
    }

    Vector2 UITextComponent::GetMeasuredSize() const
    {
        return { measuredSizeEm_.x * fontSize_, measuredSizeEm_.y * fontSize_ };
    }

    bool UITextComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const RectTransformComponent*>(&other) != nullptr;
    }

    RectTransformComponent* UITextComponent::GetRectTransform() const
    {
        if (!rect_) {
            rect_ = Sibling<RectTransformComponent>();
        }
        return rect_;
    }

    void UITextComponent::UpdateGeometry(RectTransformComponent& rect)
    {
        // 文字列に合わせた後で大きさを変えられたら、その大きさの枠に固定する
        if (fieldAutoFit_ && hasFitted_) {
            const Vector2 size = rect.GetSize();
            if (size.x != fittedSize_.x || size.y != fittedSize_.y) {
                fieldAutoFit_ = false;
                hasFitted_ = false;
                geometryDirty_ = true;
            }
        }

        // 基準点か枠が変わった・実行時ベイクでグリフが増えたら組み直す
        if (rect.GetShapeRevision() != builtShapeRevision_ ||
            font_->GetGlyphGeneration() != builtGlyphGeneration_) {
            geometryDirty_ = true;
        }

        if (geometryDirty_) {
            RebuildGeometry(rect);
        } else if (fitDirty_) {
            FitRect(rect);
        }
    }

    void UITextComponent::RebuildGeometry(RectTransformComponent& rect)
    {
        geometryDirty_ = false;
        fitDirty_ = false;
        glyphVertices_.clear();
        measuredSizeEm_ = { 0.0f, 0.0f };
        lineCount_ = 0;

        if (!font_ || !font_->IsValid()) { return; }

        // 折り返し幅と枠は px なので em へ直す。枠を固定しているならその幅で折る
        const Vector2 fieldSize = rect.GetSize();
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

    void UITextComponent::FitRect(RectTransformComponent& rect)
    {
        fitDirty_ = false;

        // 行が 1 つも無い（空文字列）ときは大きさを 0 へ潰さない。潰すと Canvas で選び直せなくなる
        if (lineCount_ > 0) {
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
        if (!rect) { return; }

        UpdateGeometry(*rect);
        if (glyphVertices_.empty()) { return; }

        const UILayout& layout = rect->GetLayout();
        const Vector2 screenPosition = layout.CalculateScreenPosition(renderer_->GetScreenSize());
        const Vector3 position = { screenPosition.x, screenPosition.y, 0.0f };
        // 頂点が em 単位なので、フォントサイズがそのまま拡大率になる
        const Vector3 scale = { fontSize_, fontSize_, 1.0f };
        const Vector3 rotation = { 0.0f, 0.0f, layout.rotation };
        const Matrix4x4 world = Matrix::MakeAffine(scale, rotation, position);

        // ドローコールは出さず、レンダラーのバッチへ積むだけ
        renderer_->Submit(font_, glyphVertices_.data(), glyphVertices_.size(), world, style_);
    }
}
