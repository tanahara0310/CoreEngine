#include "pch.h"
#include "UI/UIInputFieldComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Input/TextInput.h"
#include "Text/TextEncoding.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "UI/UIPointer.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>

namespace CoreEngine
{
    namespace
    {
        using Reflection::PropertyDescriptor;
        using Reflection::PropertyFlags;
        using Reflection::PropertyType;

        /// @brief スクリプトから読むだけの、インスペクタに出さず保存もしない真偽値のプロパティ
        template <bool (UIInputFieldComponent::*Getter)() const>
        PropertyDescriptor MakeHiddenFlag(const char* name, const char* displayName)
        {
            PropertyDescriptor p;
            p.name = name;
            p.displayName = displayName;
            p.type = PropertyType::Bool;
            p.flags = PropertyFlags::ReadOnly | PropertyFlags::Hidden;
            p.get = [](const PropertyDescriptor&, const void* o, void* out) {
                *static_cast<bool*>(out) = (static_cast<const UIInputFieldComponent*>(o)->*Getter)();
            };
            p.set = [](const PropertyDescriptor&, void*, const void*) {};
            return p;
        }
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::UIInputFieldComponent, "UI 入力欄")
    REFLECT_ENUM_ACCESSOR("contentType", "中身の種類", GetContentType, SetContentType, Self::kContentTypeNames,
        p.tooltip = "整数・小数・英数字は合わない文字を受け付けない。伏せ字は * で出す（日本語の変換は使わない）")
    REFLECT_ACCESSOR("characterLimit", "最大の文字数", GetCharacterLimit, SetCharacterLimit,
        p.tooltip = "0 なら決めない")
    REFLECT_ACCESSOR("multiLine", "複数行", IsMultiLine, SetMultiLine,
        p.tooltip = "Enter で改行し、長い文は折り返す。確定は Ctrl+Enter か、欄の外を押す")
    REFLECT_ACCESSOR("readOnly", "読み取りだけ", IsReadOnly, SetReadOnly,
        p.tooltip = "選ぶ・コピーはできるが、文字は変えられない")
    REFLECT_ACCESSOR("text", "文字", GetText, SetText)
    REFLECT_ACCESSOR("interactable", "押せる", IsInteractable, SetInteractable,
        p.tooltip = "外すと押せない色になり、ポインタもフォーカスも受け付けなくなる")
    REFLECT_OBJECT_REF(textComponent_, "文字を出す UI テキスト")
    REFLECT_OBJECT_REF(placeholder_, "置き文字")
    REFLECT_PROPERTY(caretColor_, "カーソルの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(selectionColor_, "選択の色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(normalColor_, "通常の色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(hoveredColor_, "乗せたときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color,
        p.tooltip = "キー・パッドで選んでいるとき・打っているときもこの色になる")
    REFLECT_PROPERTY(pressedColor_, "押したときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(disabledColor_, "押せないときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    d.properties.push_back(MakeHiddenFlag<&Self::IsEditing>("isFocused", "打っている"));
    d.properties.push_back(MakeHiddenFlag<&Self::WasChanged>("wasChanged", "文字が変わった"));
    d.properties.push_back(MakeHiddenFlag<&Self::WasSubmitted>("wasSubmitted", "確定した"));
    REFLECT_METHOD("Focus", "打ち始める", Focus)
    REFLECT_METHOD("Unfocus", "打ち終える", Unfocus)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::UIInputFieldComponent)
COMPONENT_REGISTER(CoreEngine::UIInputFieldComponent)

namespace CoreEngine
{
    namespace
    {
        /// カーソルの幅［px］
        constexpr float kCaretWidth = 2.0f;

        /// カーソルの点滅の周期［秒］（前半に出し、後半に消す）
        constexpr float kBlinkPeriod = 1.0f;

        /// 文字の無い所を選んだときに出す幅（フォントの大きさに対する割合）
        constexpr float kEmptySelectionEm = 0.3f;

        /// 変換中の文字の下線の太さ（フォントの大きさに対する割合）
        constexpr float kUnderlineEm = 0.06f;

        /// 伏せ字で出す文字
        constexpr char32_t kMaskCharacter = U'*';

        bool IsDigit(char32_t c) { return c >= U'0' && c <= U'9'; }

        bool IsAsciiLetter(char32_t c) { return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'); }

        bool IsSpace(char32_t c) { return c == U' ' || c == U'\t' || c == U'\n' || c == 0x3000; }

        std::u32string ToUtf32(const std::string& utf8)
        {
            const std::vector<char32_t> codePoints = Utf8ToUtf32(utf8);
            return std::u32string(codePoints.begin(), codePoints.end());
        }

        bool SameVector(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }

        bool SameColor(const Vector4& a, const Vector4& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        bool SameLayout(const UILayout& a, const UILayout& b)
        {
            return SameVector(a.anchorMin, b.anchorMin) && SameVector(a.anchorMax, b.anchorMax)
                && SameVector(a.anchoredPos, b.anchoredPos) && SameVector(a.pivot, b.pivot)
                && SameVector(a.size, b.size) && a.rotation == b.rotation
                && SameVector(a.scale, b.scale) && a.sortOrder == b.sortOrder;
        }

        /// @brief オブジェクトのアクティブを変える（持ち主が無い・同じなら何もしない）
        void SetObjectActive(const IComponent* component, bool active)
        {
            GameObject* const owner = component ? component->GetOwner() : nullptr;
            if (owner && owner->IsActive() != active) {
                owner->SetActive(active);
            }
        }

        /// @brief 根の基準点から測った四角に置く
        void PlaceQuad(UIImageComponent& quad, const Vector2& anchor, const Vector2& position,
                       const Vector2& size, const Vector4& color, bool visible)
        {
            GameObject* const owner = quad.GetOwner();
            RectTransformComponent* const rect = owner ? owner->GetComponent<RectTransformComponent>() : nullptr;
            if (!rect) {
                return;
            }
            UILayout layout;
            layout.anchorMin = anchor;
            layout.anchorMax = anchor;
            layout.pivot = { 0.0f, 0.0f };
            layout.anchoredPos = position;
            layout.size = size;
            if (!SameLayout(rect->GetLayout(), layout)) {
                rect->SetLayout(layout);
            }
            if (!SameColor(quad.GetColor(), color)) {
                quad.SetColor(color);
            }
            SetObjectActive(&quad, visible);
        }
    }

    UIInputFieldComponent::UIInputFieldComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.93f, 0.96f, 1.0f, 1.0f },
                                { 0.85f, 0.90f, 1.0f, 1.0f }, { 0.80f, 0.80f, 0.80f, 0.6f })
    {
    }

    void UIInputFieldComponent::Start()
    {
        started_ = true;
        RefreshDisplay();
    }

    void UIInputFieldComponent::OnDisable()
    {
        FinishEditing(false, false);
        HideVisuals();
    }

    void UIInputFieldComponent::OnDestroy()
    {
        if (editing_) {
            TextInput::Get().EndCapture(captureToken_);
            editing_ = false;
            captureToken_ = 0;
        }
        DestroyVisuals();
    }

    void UIInputFieldComponent::LateUpdate()
    {
        if (!editing_) {
            return;
        }

        // ほかの欄が打ち始めた・アプリが裏へ回った・押せなくなった・隠れたら、打った文字のまま終える
        TextInput& input = TextInput::Get();
        const RectTransformComponent* const rect = GetRectTransform();
        const bool usable = AcceptsInput() && rect && rect->IsActiveInHierarchy() && textComponent_.Get();
        if (!input.IsCapturedBy(captureToken_) || !usable) {
            FinishEditing(false, multiLine_);
            return;
        }

        const std::vector<TextInputEvent> events = input.TakeEvents();
        if (!events.empty()) {
            ApplyEvents(events);
            if (!editing_) {
                return;
            }
        }

        // 変換を始めたら、選んでいた範囲は変換した文字で置き換える
        const std::u32string& composing = input.GetComposition();
        if (!composing.empty() && HasSelection() && !readOnly_) {
            if (DeleteSelection()) {
                OnValueEdited();
            }
        }
        if (composing != composition_ || input.GetCompositionCursor() != compositionCursor_) {
            composition_ = composing;
            compositionCursor_ = std::clamp(input.GetCompositionCursor(), 0, static_cast<int>(composition_.size()));
            blinkTime_ = 0.0f;
        }

        blinkTime_ = std::fmod(blinkTime_ + Time::UnscaledDeltaTime(), kBlinkPeriod);
        RefreshDisplay();
        RefreshVisuals();
    }

    void UIInputFieldComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        UISelectableComponent::OnPropertyChanged(property);
        if (std::string_view(property.name) == "multiLine") {
            ApplyLineLayout();
        }
        RefreshDisplay();
    }

    // ===== 問い合わせ =====

    bool UIInputFieldComponent::WasChanged() const
    {
        return IsFrameAfter(changedFrame_);
    }

    bool UIInputFieldComponent::WasSubmitted() const
    {
        return IsFrameAfter(submittedFrame_);
    }

    // ===== 文字 =====

    void UIInputFieldComponent::SetText(const std::string& text)
    {
        const std::u32string sanitized = Sanitize(ToUtf32(text));
        if (editing_) {
            value_ = sanitized;
            caret_ = static_cast<int>(value_.size());
            anchor_ = caret_;
            hasPreferredX_ = false;
        }
        const std::string next = Utf32ToUtf8(sanitized);
        if (next != text_) {
            text_ = next;
            // 読み込みで値を流したときは、変わったことにしない
            if (started_) {
                changedFrame_ = Time::FrameCount();
            }
        }
        RefreshDisplay();
    }

    void UIInputFieldComponent::SetCharacterLimit(int limit)
    {
        limit = (std::max)(limit, 0);
        if (limit == characterLimit_) {
            return;
        }
        characterLimit_ = limit;
        Revalidate();
    }

    void UIInputFieldComponent::SetContentType(UIInputContentType type)
    {
        if (type == contentType_) {
            return;
        }
        contentType_ = type;
        Revalidate();
    }

    void UIInputFieldComponent::SetMultiLine(bool multiLine)
    {
        if (multiLine == multiLine_) {
            return;
        }
        multiLine_ = multiLine;
        Revalidate();
    }

    void UIInputFieldComponent::SetReadOnly(bool readOnly)
    {
        readOnly_ = readOnly;
    }

    void UIInputFieldComponent::Revalidate()
    {
        const std::u32string sanitized = Sanitize(editing_ ? value_ : ToUtf32(text_));
        if (editing_) {
            value_ = sanitized;
            caret_ = (std::min)(caret_, static_cast<int>(value_.size()));
            anchor_ = (std::min)(anchor_, static_cast<int>(value_.size()));
        }
        const std::string next = Utf32ToUtf8(sanitized);
        if (next != text_) {
            text_ = next;
            if (started_) {
                changedFrame_ = Time::FrameCount();
            }
        }
        RefreshDisplay();
    }

    bool UIInputFieldComponent::IsAllowed(char32_t character, int position, const std::u32string& current) const
    {
        if (character == U'\n') {
            return multiLine_ && (contentType_ == UIInputContentType::Standard
                               || contentType_ == UIInputContentType::Password);
        }
        if (character < 0x20 || character == 0x7F) {
            return false;
        }

        switch (contentType_) {
        case UIInputContentType::Integer:
        case UIInputContentType::Decimal: {
            // - は先頭に 1 つだけ。- より前には何も入れない
            const bool hasMinus = !current.empty() && current.front() == U'-';
            if (IsDigit(character)) {
                return !(position == 0 && hasMinus);
            }
            if (character == U'-') {
                return position == 0 && !hasMinus;
            }
            if (character == U'.' && contentType_ == UIInputContentType::Decimal) {
                return current.find(U'.') == std::u32string::npos && !(position == 0 && hasMinus);
            }
            return false;
        }
        case UIInputContentType::Alphanumeric:
            return IsDigit(character) || IsAsciiLetter(character);
        default:
            return true;
        }
    }

    std::u32string UIInputFieldComponent::Sanitize(const std::u32string& text) const
    {
        std::u32string result;
        result.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            char32_t character = text[i];
            // 改行は \n にそろえる
            if (character == U'\r') {
                if (i + 1 < text.size() && text[i + 1] == U'\n') {
                    continue;
                }
                character = U'\n';
            }
            if (characterLimit_ > 0 && static_cast<int>(result.size()) >= characterLimit_) {
                break;
            }
            if (IsAllowed(character, static_cast<int>(result.size()), result)) {
                result.push_back(character);
            }
        }
        return result;
    }

    // ===== 繋ぎ =====

    void UIInputFieldComponent::SetTextComponent(UITextComponent* text)
    {
        FinishEditing(false, false);
        DestroyVisuals();
        textComponent_.Set(text);
        RefreshDisplay();
    }

    void UIInputFieldComponent::SetPlaceholder(UITextComponent* placeholder)
    {
        placeholder_.Set(placeholder);
        RefreshDisplay();
    }

    void UIInputFieldComponent::ApplyLineLayout()
    {
        UITextComponent* const texts[] = { textComponent_.Get(), placeholder_.Get() };
        for (UITextComponent* const text : texts) {
            RectTransformComponent* const rect = text ? text->GetRectTransform() : nullptr;
            if (!rect) {
                continue;
            }
            UILayout layout = rect->GetLayout();
            if (multiLine_) {
                // 欄いっぱいに広げ、その幅で折り返して上から並べる
                text->SetFieldAutoFit(false);
                text->SetAlign(TextAlignH::Left, TextAlignV::Top);
                layout.anchorMin = { 0.0f, 0.0f };
                layout.anchorMax = { 1.0f, 1.0f };
                layout.pivot = { 0.5f, 0.5f };
                layout.size = { 0.0f, 0.0f };
            } else {
                // 左の真ん中に付け、文字の分だけ右へ伸ばす
                text->SetFieldAutoFit(true);
                text->SetAlign(TextAlignH::Left, TextAlignV::Middle);
                layout.anchorMin = { 0.0f, 0.5f };
                layout.anchorMax = { 0.0f, 0.5f };
                layout.pivot = { 0.0f, 0.5f };
            }
            layout.anchoredPos = { 0.0f, 0.0f };
            rect->SetLayout(layout);
        }
    }

    // ===== 打つ =====

    void UIInputFieldComponent::Focus()
    {
        BeginEditing();
    }

    void UIInputFieldComponent::EndEditing(bool cancel)
    {
        FinishEditing(cancel, !cancel && multiLine_);
    }

    void UIInputFieldComponent::BeginEditing(bool refreshVisuals)
    {
        if (editing_ || !AcceptsInput()) {
            return;
        }
        const RectTransformComponent* const rect = GetRectTransform();
        UITextComponent* const text = textComponent_.Get();
        RectTransformComponent* const textRect = text ? text->GetRectTransform() : nullptr;
        if (!rect || !rect->IsActiveInHierarchy()) {
            return;
        }
        if (!textRect) {
            if (!warned_) {
                warned_ = true;
                const GameObject* const owner = GetOwner();
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "UIInputField \"{}\": 文字を出す UI テキストが繋がっていないので打てません",
                    owner ? owner->GetName() : std::string());
            }
            return;
        }

        textBeforeEdit_ = text_;
        value_ = ToUtf32(text_);
        caret_ = static_cast<int>(value_.size());
        anchor_ = caret_;
        hasPreferredX_ = false;
        composition_.clear();
        compositionCursor_ = 0;

        // 日本語の変換は、なんでも入れられる欄だけで使う
        const bool allowIme = contentType_ == UIInputContentType::Standard && !readOnly_;
        captureToken_ = TextInput::Get().BeginCapture(allowIme);
        editing_ = true;

        textBasePosition_ = textRect->GetAnchoredPosition();
        scroll_ = { 0.0f, 0.0f };
        blinkTime_ = 0.0f;
        caretLayoutValid_ = false;

        ApplyVisual();
        RefreshDisplay();
        if (refreshVisuals) {
            RefreshVisuals();
        }
    }

    void UIInputFieldComponent::FinishEditing(bool restore, bool submitted)
    {
        if (!editing_) {
            return;
        }
        editing_ = false;
        TextInput::Get().EndCapture(captureToken_);
        captureToken_ = 0;
        composition_.clear();
        compositionCursor_ = 0;
        pointerPressPending_ = false;
        pointerSelecting_ = false;

        if (restore && text_ != textBeforeEdit_) {
            text_ = textBeforeEdit_;
            changedFrame_ = Time::FrameCount();
        }
        if (submitted) {
            submittedFrame_ = Time::FrameCount();
        }
        value_.clear();

        // ずらしていた文字を元の位置へ戻す
        scroll_ = { 0.0f, 0.0f };
        if (UITextComponent* const text = textComponent_.Get()) {
            if (RectTransformComponent* const textRect = text->GetRectTransform()) {
                if (!SameVector(textRect->GetAnchoredPosition(), textBasePosition_)) {
                    textRect->SetAnchoredPosition(textBasePosition_);
                }
            }
        }

        HideVisuals();
        RefreshDisplay();
        ApplyVisual();
    }

    // ===== IUIInteractable =====

    void UIInputFieldComponent::OnPressBegin()
    {
        UISelectableComponent::OnPressBegin();
        pointerPressPending_ = true;
    }

    void UIInputFieldComponent::OnPressEnd(bool onSelf)
    {
        pointerPressPending_ = false;
        pointerSelecting_ = false;
        UISelectableComponent::OnPressEnd(onSelf);
    }

    void UIInputFieldComponent::OnDrag(const Vector2& pointerOnCanvas, const Vector2& canvasSize)
    {
        if (!pointerPressPending_ && !pointerSelecting_) {
            return;
        }

        bool extend = true;
        if (pointerPressPending_) {
            pointerPressPending_ = false;
            if (!editing_) {
                BeginEditing(false);
            }
            if (!editing_) {
                return;
            }
            // 変換中に押したら、変換を確定させるだけにする（確定した文字は今のカーソルの所へ入る）
            if (!composition_.empty()) {
                TextInput::Get().CompleteComposition();
                return;
            }
            pointerSelecting_ = true;
            extend = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        }

        UITextComponent* const text = textComponent_.Get();
        const RectTransformComponent* const textRect = text ? text->GetRectTransform() : nullptr;
        Vector2 local{};
        if (!textRect || !textRect->CanvasToLocal(pointerOnCanvas, canvasSize, local) || !EnsureCaretLayout()) {
            return;
        }
        const int position = DisplayToValue(HitTest(local));
        if (position != caret_ || !extend) {
            MoveCaret(position, extend);
            hasPreferredX_ = false;
            blinkTime_ = 0.0f;
            RefreshVisuals();
        }
    }

    bool UIInputFieldComponent::OnNavigate(UINavigationDirection direction)
    {
        (void)direction;
        return editing_;
    }

    void UIInputFieldComponent::OnClick()
    {
        Focus();
    }

    void UIInputFieldComponent::ApplyVisual()
    {
        if (editing_ && IsInteractable() && !IsPressed()) {
            if (UIImageComponent* const image = Sibling<UIImageComponent>()) {
                image->SetColor(hoveredColor_);
            }
            return;
        }
        UISelectableComponent::ApplyVisual();
    }

    // ===== 文字を直す =====

    void UIInputFieldComponent::ApplyEvents(const std::vector<TextInputEvent>& events)
    {
        for (const TextInputEvent& event : events) {
            if (event.kind == TextInputEvent::Kind::Character) {
                if (InsertText(std::u32string(1, event.character))) {
                    OnValueEdited();
                }
                hasPreferredX_ = false;
            } else {
                ApplyKey(event);
            }
            blinkTime_ = 0.0f;
            if (!editing_) {
                return;
            }
        }
    }

    void UIInputFieldComponent::ApplyKey(const TextInputEvent& event)
    {
        const int length = static_cast<int>(value_.size());
        const bool verticalKey = event.key == VK_UP || event.key == VK_DOWN;
        if (!verticalKey) {
            hasPreferredX_ = false;
        }

        switch (event.key) {
        case VK_LEFT:
            if (HasSelection() && !event.shift) {
                MoveCaret(SelectionStart(), false);
            } else {
                MoveCaret(event.ctrl ? PreviousWordBoundary(caret_) : caret_ - 1, event.shift);
            }
            break;
        case VK_RIGHT:
            if (HasSelection() && !event.shift) {
                MoveCaret(SelectionEnd(), false);
            } else {
                MoveCaret(event.ctrl ? NextWordBoundary(caret_) : caret_ + 1, event.shift);
            }
            break;
        case VK_UP:
        case VK_DOWN:
            if (multiLine_) {
                MoveVertical(event.key == VK_UP ? -1 : 1, event.shift);
            } else {
                MoveCaret(event.key == VK_UP ? 0 : length, event.shift);
            }
            break;
        case VK_HOME:
            MoveCaret((multiLine_ && !event.ctrl) ? LineStart(caret_) : 0, event.shift);
            break;
        case VK_END:
            MoveCaret((multiLine_ && !event.ctrl) ? LineEnd(caret_) : length, event.shift);
            break;
        case VK_BACK:
            if (readOnly_) {
                break;
            }
            if (HasSelection()) {
                if (DeleteSelection()) {
                    OnValueEdited();
                }
            } else if (caret_ > 0) {
                const int from = event.ctrl ? PreviousWordBoundary(caret_) : caret_ - 1;
                value_.erase(static_cast<std::size_t>(from), static_cast<std::size_t>(caret_ - from));
                caret_ = from;
                anchor_ = from;
                OnValueEdited();
            }
            break;
        case VK_DELETE:
            if (readOnly_) {
                break;
            }
            if (HasSelection()) {
                if (DeleteSelection()) {
                    OnValueEdited();
                }
            } else if (caret_ < length) {
                const int to = event.ctrl ? NextWordBoundary(caret_) : caret_ + 1;
                value_.erase(static_cast<std::size_t>(caret_), static_cast<std::size_t>(to - caret_));
                anchor_ = caret_;
                OnValueEdited();
            }
            break;
        case VK_RETURN:
            if (multiLine_ && !event.ctrl) {
                if (InsertText(U"\n")) {
                    OnValueEdited();
                }
            } else {
                FinishEditing(false, true);
            }
            break;
        case VK_ESCAPE:
            FinishEditing(true, false);
            break;
        case 'A':
            if (event.ctrl) {
                anchor_ = 0;
                caret_ = length;
            }
            break;
        case 'C':
            if (event.ctrl) {
                CopySelection();
            }
            break;
        case 'X':
            if (event.ctrl && contentType_ != UIInputContentType::Password) {
                CopySelection();
                if (!readOnly_ && DeleteSelection()) {
                    OnValueEdited();
                }
            }
            break;
        case 'V':
            if (event.ctrl && !readOnly_) {
                if (InsertText(TextInput::Get().GetClipboardText())) {
                    OnValueEdited();
                }
            }
            break;
        default:
            break;
        }
    }

    bool UIInputFieldComponent::InsertText(const std::u32string& text)
    {
        if (readOnly_ || text.empty()) {
            return false;
        }
        bool changed = DeleteSelection();
        for (std::size_t i = 0; i < text.size(); ++i) {
            char32_t character = text[i];
            if (character == U'\r') {
                if (i + 1 < text.size() && text[i + 1] == U'\n') {
                    continue;
                }
                character = U'\n';
            }
            if (characterLimit_ > 0 && static_cast<int>(value_.size()) >= characterLimit_) {
                break;
            }
            if (!IsAllowed(character, caret_, value_)) {
                continue;
            }
            value_.insert(value_.begin() + caret_, character);
            ++caret_;
            changed = true;
        }
        anchor_ = caret_;
        return changed;
    }

    bool UIInputFieldComponent::DeleteSelection()
    {
        if (!HasSelection()) {
            return false;
        }
        const int start = SelectionStart();
        const int end = SelectionEnd();
        value_.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        caret_ = start;
        anchor_ = start;
        return true;
    }

    void UIInputFieldComponent::MoveCaret(int position, bool extend)
    {
        caret_ = std::clamp(position, 0, static_cast<int>(value_.size()));
        if (!extend) {
            anchor_ = caret_;
        }
    }

    int UIInputFieldComponent::PreviousWordBoundary(int position) const
    {
        while (position > 0 && IsSpace(value_[static_cast<std::size_t>(position - 1)])) {
            --position;
        }
        while (position > 0 && !IsSpace(value_[static_cast<std::size_t>(position - 1)])) {
            --position;
        }
        return position;
    }

    int UIInputFieldComponent::NextWordBoundary(int position) const
    {
        const int length = static_cast<int>(value_.size());
        while (position < length && !IsSpace(value_[static_cast<std::size_t>(position)])) {
            ++position;
        }
        while (position < length && IsSpace(value_[static_cast<std::size_t>(position)])) {
            ++position;
        }
        return position;
    }

    int UIInputFieldComponent::LineStart(int position)
    {
        if (!EnsureCaretLayout()) {
            return position;
        }
        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        const int index = ValueToDisplay(position);
        const uint32_t line = stops[static_cast<std::size_t>(index)].line;
        int start = index;
        while (start > 0 && stops[static_cast<std::size_t>(start - 1)].line == line) {
            --start;
        }
        return DisplayToValue(start);
    }

    int UIInputFieldComponent::LineEnd(int position)
    {
        if (!EnsureCaretLayout()) {
            return position;
        }
        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        const int index = ValueToDisplay(position);
        const uint32_t line = stops[static_cast<std::size_t>(index)].line;
        int end = index;
        while (end + 1 < static_cast<int>(stops.size()) && stops[static_cast<std::size_t>(end + 1)].line == line) {
            ++end;
        }
        return DisplayToValue(end);
    }

    void UIInputFieldComponent::MoveVertical(int direction, bool extend)
    {
        if (!EnsureCaretLayout()) {
            return;
        }
        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        const UITextCaretStop& current = stops[static_cast<std::size_t>(ValueToDisplay(caret_))];
        if (!hasPreferredX_) {
            preferredX_ = current.position.x;
            hasPreferredX_ = true;
        }

        const int target = static_cast<int>(current.line) + direction;
        if (target < 0) {
            MoveCaret(0, extend);
            return;
        }
        if (target >= static_cast<int>(caretLayout_.lineCount)) {
            MoveCaret(static_cast<int>(value_.size()), extend);
            return;
        }

        int best = -1;
        float bestDistance = 0.0f;
        for (std::size_t i = 0; i < stops.size(); ++i) {
            if (static_cast<int>(stops[i].line) != target) {
                continue;
            }
            const float distance = std::fabs(stops[i].position.x - preferredX_);
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        if (best >= 0) {
            MoveCaret(DisplayToValue(best), extend);
        }
    }

    void UIInputFieldComponent::CopySelection() const
    {
        // 伏せた文字は外へ出さない
        if (!HasSelection() || contentType_ == UIInputContentType::Password) {
            return;
        }
        const int start = SelectionStart();
        TextInput::Get().SetClipboardText(value_.substr(static_cast<std::size_t>(start),
                                                        static_cast<std::size_t>(SelectionEnd() - start)));
    }

    void UIInputFieldComponent::OnValueEdited()
    {
        text_ = Utf32ToUtf8(value_);
        changedFrame_ = Time::FrameCount();
        blinkTime_ = 0.0f;
    }

    // ===== 出す =====

    std::u32string UIInputFieldComponent::BuildDisplay() const
    {
        std::u32string display = editing_ ? value_ : ToUtf32(text_);
        if (editing_ && !composition_.empty()) {
            display.insert(static_cast<std::size_t>(caret_), composition_);
        }
        if (contentType_ == UIInputContentType::Password) {
            for (char32_t& character : display) {
                if (character != U'\n') {
                    character = kMaskCharacter;
                }
            }
        }
        return display;
    }

    void UIInputFieldComponent::RefreshDisplay()
    {
        const std::u32string display = BuildDisplay();
        if (UITextComponent* const text = textComponent_.Get()) {
            const std::string utf8 = Utf32ToUtf8(display);
            if (text->GetText() != utf8) {
                text->SetText(utf8);
            }
        }
        SetObjectActive(placeholder_.Get(), display.empty());
    }

    bool UIInputFieldComponent::EnsureCaretLayout(bool force)
    {
        UITextComponent* const text = textComponent_.Get();
        if (!text) {
            caretLayoutValid_ = false;
            return false;
        }
        const std::u32string display = BuildDisplay();
        if (!force && caretLayoutValid_ && display == measuredDisplay_) {
            return true;
        }
        measuredDisplay_ = display;
        caretLayoutValid_ = text->MeasureCarets(Utf32ToUtf8(display), caretLayout_)
            && caretLayout_.stops.size() == display.size() + 1
            && caretLayout_.lineCount > 0;
        return caretLayoutValid_;
    }

    int UIInputFieldComponent::DisplayToValue(int displayIndex) const
    {
        const int length = static_cast<int>(value_.size());
        const int composing = static_cast<int>(composition_.size());
        if (composing == 0 || displayIndex <= caret_) {
            return std::clamp(displayIndex, 0, length);
        }
        if (displayIndex >= caret_ + composing) {
            return std::clamp(displayIndex - composing, 0, length);
        }
        return caret_;
    }

    int UIInputFieldComponent::ValueToDisplay(int valueIndex) const
    {
        const int composing = static_cast<int>(composition_.size());
        return (composing == 0 || valueIndex <= caret_) ? valueIndex : valueIndex + composing;
    }

    int UIInputFieldComponent::HitTest(const Vector2& local) const
    {
        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        if (stops.empty()) {
            return 0;
        }

        // 押した高さの行を選び、その行の中でいちばん近い文字の間にする
        const float firstTop = stops.front().position.y;
        const float advance = caretLayout_.lineAdvance > 0.0f ? caretLayout_.lineAdvance : caretLayout_.lineHeight;
        int line = 0;
        if (advance > 0.0f) {
            line = static_cast<int>(std::floor((local.y - firstTop) / advance));
        }
        line = std::clamp(line, 0, static_cast<int>(caretLayout_.lineCount) - 1);

        int best = -1;
        float bestDistance = 0.0f;
        for (std::size_t i = 0; i < stops.size(); ++i) {
            if (static_cast<int>(stops[i].line) != line) {
                continue;
            }
            const float distance = std::fabs(stops[i].position.x - local.x);
            if (best < 0 || distance < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        return best < 0 ? 0 : best;
    }

    void UIInputFieldComponent::ScrollToCaret()
    {
        UITextComponent* const text = textComponent_.Get();
        RectTransformComponent* const textRect = text ? text->GetRectTransform() : nullptr;
        const RectTransformComponent* const window = textRect ? textRect->GetParent() : nullptr;
        if (!window || caretLayout_.stops.empty()) {
            return;
        }

        // 親の四角を窓にし、ずらす前の文字の基準点から測る
        const Vector2 canvasSize = textRect->FindCanvasSize();
        const Vector2 windowSize = window->GetRectSize(canvasSize);
        UILayout base = textRect->GetLayout();
        base.anchoredPos = textBasePosition_;
        const UIRect windowRect{ { 0.0f, 0.0f }, windowSize };
        const Vector2 pivot = base.CalculatePivotPosition(windowRect);

        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        const int caretIndex = std::clamp(composition_.empty() ? ValueToDisplay(caret_) : caret_ + compositionCursor_,
                                          0, static_cast<int>(stops.size()) - 1);
        const UITextCaretStop& caret = stops[static_cast<std::size_t>(caretIndex)];

        if (!multiLine_) {
            float end = caret.position.x;
            for (const UITextCaretStop& stop : stops) {
                end = (std::max)(end, stop.position.x);
            }
            const float right = windowSize.x - kCaretWidth;
            const float caretX = pivot.x + caret.position.x;
            float offset = scroll_.x;
            if (caretX - offset > right) {
                offset = caretX - right;
            }
            if (caretX - offset < 0.0f) {
                offset = caretX;
            }
            // 消して短くなったら、末尾が右端に来るところまで戻す
            offset = (std::min)(offset, (std::max)(0.0f, pivot.x + end - right));
            scroll_ = { (std::max)(offset, 0.0f), 0.0f };
        } else {
            float lastTop = caret.position.y;
            for (const UITextCaretStop& stop : stops) {
                lastTop = (std::max)(lastTop, stop.position.y);
            }
            const float lineHeight = caretLayout_.lineHeight;
            const float top = pivot.y + caret.position.y;
            float offset = scroll_.y;
            if (top + lineHeight - offset > windowSize.y) {
                offset = top + lineHeight - windowSize.y;
            }
            if (top - offset < 0.0f) {
                offset = top;
            }
            offset = (std::min)(offset, (std::max)(0.0f, pivot.y + lastTop + lineHeight - windowSize.y));
            scroll_ = { 0.0f, (std::max)(offset, 0.0f) };
        }

        const Vector2 next = { textBasePosition_.x - scroll_.x, textBasePosition_.y - scroll_.y };
        if (!SameVector(textRect->GetAnchoredPosition(), next)) {
            textRect->SetAnchoredPosition(next);
        }
    }

    void UIInputFieldComponent::RefreshVisuals()
    {
        UITextComponent* const text = textComponent_.Get();
        RectTransformComponent* const textRect = text ? text->GetRectTransform() : nullptr;
        if (!editing_ || !textRect || !EnsureCaretLayout(true)) {
            HideVisuals();
            return;
        }
        ScrollToCaret();
        if (!EnsureVisualRoots(*text)) {
            return;
        }
        RectTransformComponent* const back = backRoot_.Get();
        RectTransformComponent* const front = frontRoot_.Get();

        // 根は文字と同じ所に置き、選んだ範囲は文字の後ろ、カーソルと下線は文字の前に描く
        const UILayout& textLayout = textRect->GetLayout();
        UILayout backLayout = textLayout;
        backLayout.sortOrder = textLayout.sortOrder - 1;
        UILayout frontLayout = textLayout;
        frontLayout.sortOrder = textLayout.sortOrder + 1;
        RectTransformComponent* const parent = textRect->GetParent();
        for (RectTransformComponent* const root : { back, front }) {
            if (root->GetParent() != parent) {
                root->SetParent(parent, false);
            }
            const UILayout& layout = root == back ? backLayout : frontLayout;
            if (!SameLayout(root->GetLayout(), layout)) {
                root->SetLayout(layout);
            }
            SetObjectActive(root, true);
        }

        // 行ごとの左端・右端・上端
        const std::vector<UITextCaretStop>& stops = caretLayout_.stops;
        const std::size_t lineCount = caretLayout_.lineCount;
        std::vector<float> lineLeft(lineCount, (std::numeric_limits<float>::max)());
        std::vector<float> lineRight(lineCount, std::numeric_limits<float>::lowest());
        std::vector<float> lineTop(lineCount, 0.0f);
        for (const UITextCaretStop& stop : stops) {
            if (stop.line >= lineCount) {
                continue;
            }
            lineLeft[stop.line] = (std::min)(lineLeft[stop.line], stop.position.x);
            lineRight[stop.line] = (std::max)(lineRight[stop.line], stop.position.x);
            lineTop[stop.line] = stop.position.y;
        }

        // from から to まで（出す文字の中の位置）を、行ごとの帯に分ける
        const auto forEachSpan = [&](int from, int to, const auto& emit) {
            const UITextCaretStop& first = stops[static_cast<std::size_t>(from)];
            const UITextCaretStop& last = stops[static_cast<std::size_t>(to)];
            for (uint32_t line = first.line; line <= last.line && line < lineCount; ++line) {
                const float left = line == first.line ? first.position.x : lineLeft[line];
                const float right = line == last.line ? last.position.x : lineRight[line];
                emit(left, right, lineTop[line], line != last.line);
            }
            };

        const Vector2 anchor = textLayout.pivot;
        const float fontSize = text->GetFontSize();
        const float lineHeight = caretLayout_.lineHeight;
        const int maxIndex = static_cast<int>(stops.size()) - 1;

        // ---- 選んだ範囲 ----
        std::size_t selectionCount = 0;
        if (HasSelection()) {
            const int from = std::clamp(ValueToDisplay(SelectionStart()), 0, maxIndex);
            const int to = std::clamp(ValueToDisplay(SelectionEnd()), 0, maxIndex);
            forEachSpan(from, to, [&](float left, float right, float top, bool continues) {
                float width = right - left;
                // 改行をまたぐ行は、改行の分も少し塗る
                if (continues || width <= 0.0f) {
                    width = (std::max)(width, 0.0f) + fontSize * kEmptySelectionEm;
                }
                if (UIImageComponent* const quad = QuadAt(*back, selectionQuads_, selectionCount, "Selection")) {
                    PlaceQuad(*quad, anchor, { left, top }, { width, lineHeight }, selectionColor_, true);
                }
                ++selectionCount;
                });
        }
        for (std::size_t i = selectionCount; i < selectionQuads_.size(); ++i) {
            SetObjectActive(selectionQuads_[i].Get(), false);
        }

        // ---- 変換中の文字の下線 ----
        std::size_t underlineCount = 0;
        if (!composition_.empty()) {
            const int from = std::clamp(caret_, 0, maxIndex);
            const int to = std::clamp(caret_ + static_cast<int>(composition_.size()), 0, maxIndex);
            const float thickness = (std::max)(1.0f, fontSize * kUnderlineEm);
            forEachSpan(from, to, [&](float left, float right, float top, bool) {
                if (UIImageComponent* const quad = QuadAt(*front, underlineQuads_, underlineCount, "Underline")) {
                    PlaceQuad(*quad, anchor, { left, top + lineHeight - thickness },
                              { (std::max)(right - left, 0.0f), thickness }, text->GetColor(), true);
                }
                ++underlineCount;
                });
        }
        for (std::size_t i = underlineCount; i < underlineQuads_.size(); ++i) {
            SetObjectActive(underlineQuads_[i].Get(), false);
        }

        // ---- カーソル ----
        const int caretIndex = std::clamp(composition_.empty() ? ValueToDisplay(caret_) : caret_ + compositionCursor_,
                                          0, maxIndex);
        const UITextCaretStop& caret = stops[static_cast<std::size_t>(caretIndex)];
        const bool blinkOn = blinkTime_ < kBlinkPeriod * 0.5f;
        if (UIImageComponent* const quad = QuadAt(*front, caretQuads_, 0, "Caret")) {
            PlaceQuad(*quad, anchor, caret.position, { kCaretWidth, lineHeight }, caretColor_,
                      blinkOn && !HasSelection());
        }

        PlaceImeCaret(*textRect, caret.position, lineHeight);
    }

    void UIInputFieldComponent::HideVisuals()
    {
        SetObjectActive(backRoot_.Get(), false);
        SetObjectActive(frontRoot_.Get(), false);
    }

    void UIInputFieldComponent::DestroyVisuals()
    {
        const GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (manager) {
            for (const ObjectId id : visualObjects_) {
                if (GameObject* const object = manager->FindObject(id)) {
                    object->Destroy();
                }
            }
        }
        visualObjects_.clear();
        backRoot_.Reset();
        frontRoot_.Reset();
        selectionQuads_.clear();
        underlineQuads_.clear();
        caretQuads_.clear();
    }

    bool UIInputFieldComponent::EnsureVisualRoots(UITextComponent& text)
    {
        if (backRoot_.Get() && frontRoot_.Get()) {
            return true;
        }
        DestroyVisuals();

        const GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        RectTransformComponent* const textRect = text.GetRectTransform();
        if (!manager || !textRect) {
            return false;
        }

        const auto createRoot = [&](const char* name) -> RectTransformComponent* {
            auto owned = std::make_unique<GameObject>();
            owned->SetName(name);
            owned->SetSerializeEnabled(false);
            GameObject* const object = manager->AddObject(std::move(owned));
            if (!object) {
                return nullptr;
            }
            visualObjects_.push_back(object->GetObjectId());
            RectTransformComponent* const rect = object->AddComponent<RectTransformComponent>();
            rect->SetParent(textRect->GetParent(), false);
            return rect;
            };
        backRoot_.Set(createRoot("InputField Selection"));
        frontRoot_.Set(createRoot("InputField Caret"));
        return backRoot_.Get() && frontRoot_.Get();
    }

    UIImageComponent* UIInputFieldComponent::QuadAt(RectTransformComponent& root,
                                                    std::vector<ObjectRef<UIImageComponent>>& quads,
                                                    std::size_t index, const char* name)
    {
        if (index < quads.size()) {
            if (UIImageComponent* const quad = quads[index].Get()) {
                return quad;
            }
        }

        const GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (!manager) {
            return nullptr;
        }
        auto owned = std::make_unique<GameObject>();
        owned->SetName(name);
        owned->SetSerializeEnabled(false);
        GameObject* const object = manager->AddObject(std::move(owned));
        if (!object) {
            return nullptr;
        }
        visualObjects_.push_back(object->GetObjectId());
        RectTransformComponent* const rect = object->AddComponent<RectTransformComponent>();
        rect->SetParent(&root, false);
        UIImageComponent* const image = object->AddComponent<UIImageComponent>();
        if (quads.size() <= index) {
            quads.resize(index + 1);
        }
        quads[index].Set(image);
        return image;
    }

    void UIInputFieldComponent::PlaceImeCaret(const RectTransformComponent& textRect, const Vector2& caretLocal,
                                              float height) const
    {
        // 文字の座標 → キャンバス → 画面のピクセル
        const Vector2 canvasSize = textRect.FindCanvasSize();
        const Matrix4x4 matrix = textRect.GetCanvasMatrix(canvasSize);
        const auto toCanvas = [&matrix](const Vector2& point) {
            return Vector2{ point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + matrix.m[3][0],
                            point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + matrix.m[3][1] };
            };
        const Vector2 topLeft = toCanvas(caretLocal);
        const Vector2 bottomRight = toCanvas({ caretLocal.x + kCaretWidth, caretLocal.y + height });

        Vector2 screenTopLeft{};
        Vector2 screenBottomRight{};
        const UIPointer& pointer = UIPointer::Get();
        if (!pointer.CanvasToScreen(topLeft, canvasSize, screenTopLeft)
            || !pointer.CanvasToScreen(bottomRight, canvasSize, screenBottomRight)) {
            return;
        }
        RECT rect{};
        rect.left = std::lround((std::min)(screenTopLeft.x, screenBottomRight.x));
        rect.top = std::lround((std::min)(screenTopLeft.y, screenBottomRight.y));
        rect.right = std::lround((std::max)(screenTopLeft.x, screenBottomRight.x));
        rect.bottom = std::lround((std::max)(screenTopLeft.y, screenBottomRight.y));
        TextInput::Get().SetCaretRect(rect);
    }
}
