#include "pch.h"
#include "UI/UIDropdownComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIDropdownItemComponent.h"
#include "UI/UIHierarchyClone.h"
#include "UI/UIScrollViewComponent.h"
#include "UI/UISliderComponent.h"
#include "UI/UITextComponent.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <unordered_map>
#include <variant>

namespace CoreEngine
{
    namespace
    {
        using Reflection::ArrayValue;
        using Reflection::PropertyDescriptor;
        using Reflection::PropertyFlags;
        using Reflection::PropertyType;

        /// @brief 選択肢（文字の配列）のプロパティ
        PropertyDescriptor MakeOptionsProperty()
        {
            PropertyDescriptor p;
            p.name = "options";
            p.displayName = "選択肢";
            p.type = PropertyType::Array;
            p.elementType = PropertyType::String;
            p.get = [](const PropertyDescriptor&, const void* o, void* out) {
                *static_cast<ArrayValue*>(out) = static_cast<const UIDropdownComponent*>(o)->GetOptionsValue();
            };
            p.set = [](const PropertyDescriptor&, void* o, const void* in) {
                static_cast<UIDropdownComponent*>(o)->SetOptionsValue(*static_cast<const ArrayValue*>(in));
            };
            return p;
        }

        /// @brief スクリプトから読むだけの、インスペクタに出さず保存もしない真偽値のプロパティ
        template <bool (UIDropdownComponent::*Getter)() const>
        PropertyDescriptor MakeHiddenFlag(const char* name, const char* displayName)
        {
            PropertyDescriptor p;
            p.name = name;
            p.displayName = displayName;
            p.type = PropertyType::Bool;
            p.flags = PropertyFlags::ReadOnly | PropertyFlags::Hidden;
            p.get = [](const PropertyDescriptor&, const void* o, void* out) {
                *static_cast<bool*>(out) = (static_cast<const UIDropdownComponent*>(o)->*Getter)();
            };
            p.set = [](const PropertyDescriptor&, void*, const void*) {};
            return p;
        }
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::UIDropdownComponent, "UI ドロップダウン")
    d.properties.push_back(MakeOptionsProperty());
    REFLECT_ACCESSOR("value", "今の値", GetValue, SetValue,
        p.tooltip = "選んでいる選択肢の番号（0 から）")
    REFLECT_ACCESSOR("interactable", "押せる", IsInteractable, SetInteractable,
        p.tooltip = "外すと押せない色になり、ポインタもフォーカスも受け付けなくなる")
    REFLECT_OBJECT_REF(captionText_, "今の値を出す文字")
    REFLECT_OBJECT_REF(template_, "一覧のひな形")
    REFLECT_OBJECT_REF(item_, "行のひな形")
    REFLECT_OBJECT_REF(itemText_, "行の文字")
    REFLECT_PROPERTY(normalColor_, "通常の色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(hoveredColor_, "乗せたときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color,
        p.tooltip = "キー・パッドで選んでいるときもこの色になる")
    REFLECT_PROPERTY(pressedColor_, "押したときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    REFLECT_PROPERTY(disabledColor_, "押せないときの色",
        p.type = ::CoreEngine::Reflection::PropertyType::Color)
    d.properties.push_back(MakeHiddenFlag<&Self::IsOpen>("isOpen", "開いている"));
    d.properties.push_back(MakeHiddenFlag<&Self::WasChanged>("wasChanged", "値が変わった"));
    REFLECT_METHOD("Open", "一覧を開く", Open)
    REFLECT_METHOD("Close", "一覧を閉じる", Close)
    REFLECT_METHOD("AddOption", "選択肢を足す", AddOption)
    REFLECT_METHOD("RemoveOption", "選択肢を消す", RemoveOption)
    REFLECT_METHOD("ClearOptions", "選択肢を全部消す", ClearOptions)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::UIDropdownComponent)
COMPONENT_REGISTER(CoreEngine::UIDropdownComponent)

namespace CoreEngine
{
    namespace
    {
        /// @brief 置く前の一覧があるか（無ければ PlaceOpenDropdownLists は走査しない）
        std::atomic<bool> gPlacementRequested{ false };

        /// @brief オブジェクトのアクティブを変える（持ち主が無ければ何もしない）
        void SetObjectActive(const IComponent* component, bool active)
        {
            if (GameObject* const owner = component ? component->GetOwner() : nullptr) {
                owner->SetActive(active);
            }
        }
    }

    UIDropdownComponent::UIDropdownComponent()
        : UISelectableComponent({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.85f, 0.90f, 1.0f, 1.0f },
                                { 0.65f, 0.72f, 0.85f, 1.0f }, { 0.45f, 0.45f, 0.45f, 0.6f })
    {
    }

    void UIDropdownComponent::Start()
    {
        started_ = true;
        if (!open_) {
            SetObjectActive(template_.Get(), false);
        }
        SetObjectActive(item_.Get(), false);
        RefreshCaption();
    }

    void UIDropdownComponent::OnDisable()
    {
        Close();
    }

    void UIDropdownComponent::LateUpdate()
    {
        if (open_ && !IsInteractable()) {
            Close();
        }
    }

    bool UIDropdownComponent::WasChanged() const
    {
        return IsFrameAfter(changedFrame_);
    }

    // ===== 選択肢 =====

    void UIDropdownComponent::SetOptions(const std::vector<std::string>& options)
    {
        options_ = options;
        ClampValue();
        rowsDirty_ = true;
        RefreshCaption();
        if (open_) {
            BuildRows();
            RefreshSelection();
            placePending_ = true;
            gPlacementRequested = true;
        }
    }

    Reflection::ArrayValue UIDropdownComponent::GetOptionsValue() const
    {
        Reflection::ArrayValue array;
        array.elements.reserve(options_.size());
        for (const std::string& option : options_) {
            array.elements.emplace_back(std::in_place_type<std::string>, option);
        }
        return array;
    }

    void UIDropdownComponent::SetOptionsValue(const Reflection::ArrayValue& value)
    {
        std::vector<std::string> options;
        options.reserve(value.elements.size());
        for (const Reflection::ArrayValue::Element& element : value.elements) {
            const std::string* const text = std::get_if<std::string>(&element);
            options.push_back(text ? *text : std::string());
        }
        SetOptions(options);
    }

    void UIDropdownComponent::AddOption(const std::string& text)
    {
        std::vector<std::string> options = options_;
        options.push_back(text);
        SetOptions(options);
    }

    void UIDropdownComponent::RemoveOption(int index)
    {
        if (index < 0 || index >= static_cast<int>(options_.size())) {
            return;
        }
        std::vector<std::string> options = options_;
        options.erase(options.begin() + index);
        // 消した選択肢より後ろを選んでいたら、同じ選択肢を指すよう詰める
        if (value_ > index) {
            --value_;
        }
        SetOptions(options);
    }

    void UIDropdownComponent::ClearOptions()
    {
        value_ = 0;
        SetOptions({});
    }

    void UIDropdownComponent::ClampValue()
    {
        const int count = static_cast<int>(options_.size());
        if (count > 0) {
            value_ = std::clamp(value_, 0, count - 1);
        }
    }

    // ===== 値 =====

    void UIDropdownComponent::SetValue(int value)
    {
        const int count = static_cast<int>(options_.size());
        const int next = count > 0 ? std::clamp(value, 0, count - 1) : (std::max)(value, 0);
        if (next == value_) {
            return;
        }
        value_ = next;
        // 読み込みで値を流したときは、変わったことにしない
        if (started_) {
            changedFrame_ = Time::FrameCount();
        }
        RefreshCaption();
        RefreshSelection();
    }

    void UIDropdownComponent::RefreshCaption()
    {
        UITextComponent* const caption = captionText_.Get();
        if (!caption) {
            return;
        }
        const bool inRange = value_ >= 0 && value_ < static_cast<int>(options_.size());
        const std::string& text = inRange ? options_[static_cast<std::size_t>(value_)] : std::string();
        if (caption->GetText() != text) {
            caption->SetText(text);
        }
    }

    // ===== 繋ぎ =====

    void UIDropdownComponent::SetCaptionText(UITextComponent* caption)
    {
        captionText_.Set(caption);
        RefreshCaption();
    }

    void UIDropdownComponent::SetTemplate(RectTransformComponent* list)
    {
        Close();
        template_.Set(list);
    }

    void UIDropdownComponent::SetItem(UIDropdownItemComponent* item, UITextComponent* itemText)
    {
        Close();
        DestroyRows();
        item_.Set(item);
        itemText_.Set(itemText);
        rowsDirty_ = true;
    }

    // ===== 開け閉め =====

    void UIDropdownComponent::OnClick()
    {
        if (open_) {
            Close();
        } else {
            Open();
        }
    }

    void UIDropdownComponent::Open()
    {
        if (open_ || !AcceptsInput()) {
            return;
        }
        RectTransformComponent* const list = template_.Get();
        GameObject* const listObject = list ? list->GetOwner() : nullptr;
        GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (!listObject || !manager || !item_.Get()) {
            if (!warned_) {
                warned_ = true;
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "UIDropdown \"{}\": 一覧のひな形か行のひな形が繋がっていないので開けません",
                    owner ? owner->GetName() : std::string());
            }
            return;
        }

        // 開いている一覧は 1 つだけにする
        manager->ForEachComponent<UIDropdownComponent>([this](UIDropdownComponent& other) {
            if (&other != this) {
                other.Close();
            }
            });

        open_ = true;
        openList_.Set(list);
        listLayout_ = list->GetLayout();
        listObject->SetActive(true);
        list->SetDrawnOnTop(true);
        if (rowsDirty_) {
            BuildRows();
        }
        RefreshSelection();
        placePending_ = true;
        gPlacementRequested = true;
    }

    void UIDropdownComponent::Close()
    {
        if (!open_) {
            return;
        }
        open_ = false;
        placePending_ = false;
        if (RectTransformComponent* const list = openList_.Get()) {
            list->SetDrawnOnTop(false);
            list->SetLayout(listLayout_);
            SetObjectActive(list, false);
        }
        openList_.Reset();
    }

    void UIDropdownComponent::ChooseFromList(int index)
    {
        if (index >= 0 && index < static_cast<int>(options_.size())) {
            SetValue(index);
        }
        Close();
    }

    // ===== 行 =====

    void UIDropdownComponent::BuildRows()
    {
        DestroyRows();
        rowsDirty_ = false;

        UIDropdownItemComponent* const itemTemplate = item_.Get();
        GameObject* const templateObject = itemTemplate ? itemTemplate->GetOwner() : nullptr;
        GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (!templateObject || !manager) {
            return;
        }

        // ひな形は隠したまま複製し、複製だけを出す
        templateObject->SetActive(false);
        const UITextComponent* const textTemplate = itemText_.Get();
        const ObjectId textId = textTemplate && textTemplate->GetOwner()
            ? textTemplate->GetOwner()->GetObjectId() : ObjectId{};

        for (std::size_t i = 0; i < options_.size(); ++i) {
            std::unordered_map<ObjectId, GameObject*> copies;
            GameObject* const row = CloneUIHierarchy(*manager, *templateObject, &copies);
            if (!row) {
                break;
            }
            for (const auto& entry : copies) {
                rowObjects_.push_back(entry.second->GetObjectId());
            }
            row->SetName(std::format("{} {}", templateObject->GetName(), i));

            UIDropdownItemComponent* const item = row->GetComponent<UIDropdownItemComponent>();
            if (item) {
                item->Bind(this, static_cast<int>(i));
            }
            rows_.emplace_back(item);

            const auto text = copies.find(textId);
            UITextComponent* const label = text != copies.end() ? text->second->GetComponent<UITextComponent>() : nullptr;
            if (label) {
                label->SetText(options_[i]);
            }
            row->SetActive(true);
        }
    }

    void UIDropdownComponent::DestroyRows()
    {
        const GameObject* const owner = GetOwner();
        GameObjectManager* const manager = owner ? owner->GetObjectManager() : nullptr;
        if (manager) {
            for (const ObjectId id : rowObjects_) {
                if (GameObject* const object = manager->FindObject(id)) {
                    object->Destroy();
                }
            }
            if (!rowObjects_.empty()) {
                manager->InvalidateReferences();
            }
        }
        rowObjects_.clear();
        rows_.clear();
    }

    UIDropdownItemComponent* UIDropdownComponent::FindRow(int index) const
    {
        if (index < 0 || index >= static_cast<int>(rows_.size())) {
            return nullptr;
        }
        return rows_[static_cast<std::size_t>(index)].Get();
    }

    void UIDropdownComponent::RefreshSelection()
    {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (UIDropdownItemComponent* const row = rows_[i].Get()) {
                row->SetSelected(static_cast<int>(i) == value_);
            }
        }
    }

    UIScrollViewComponent* UIDropdownComponent::FindListScroll() const
    {
        const RectTransformComponent* const list = template_.Get();
        const GameObject* const listObject = list ? list->GetOwner() : nullptr;
        if (!listObject) {
            return nullptr;
        }
        if (UIScrollViewComponent* const scroll = listObject->GetComponent<UIScrollViewComponent>()) {
            return scroll;
        }
        const UIDropdownItemComponent* const item = item_.Get();
        const RectTransformComponent* current = item ? item->GetRectTransform() : nullptr;
        for (int depth = 0; current && current != list && depth < RectTransformComponent::kMaxDepth; ++depth) {
            const GameObject* const object = current->GetOwner();
            if (UIScrollViewComponent* const scroll = object ? object->GetComponent<UIScrollViewComponent>() : nullptr) {
                return scroll;
            }
            current = current->GetParent();
        }
        return nullptr;
    }

    void UIDropdownComponent::PlaceListIfPending()
    {
        if (!placePending_ || !open_) {
            return;
        }
        placePending_ = false;
        RectTransformComponent* const list = openList_.Get();
        if (!list) {
            return;
        }
        const Vector2 canvasSize = list->FindCanvasSize();
        list->SetLayout(listLayout_);

        // 行が少なければ、一覧を行の分まで縮めてスクロールバーを隠す
        UIScrollViewComponent* const scroll = FindListScroll();
        RectTransformComponent* const view = scroll ? scroll->GetRectTransform() : nullptr;
        RectTransformComponent* const content = scroll ? scroll->GetContent() : nullptr;
        if (view && content) {
            const Vector2 listSize = list->GetRectSize(canvasSize);
            const float needed = content->GetRectSize(canvasSize).y + (listSize.y - view->GetRectSize(canvasSize).y);
            const bool fits = needed <= listSize.y;
            if (fits) {
                list->SetRectSize({ listSize.x, needed }, canvasSize);
            }
            if (UISliderComponent* const bar = scroll->GetVerticalScrollbar()) {
                SetObjectActive(bar, !fits);
            }
        }

        // 画面の下にはみ出すなら、押す所の上へ開く（上もはみ出すなら、はみ出しの少ない方）
        const UIRect below = list->GetCanvasBounds(canvasSize);
        if (below.max.y > canvasSize.y) {
            const UILayout down = list->GetLayout();
            UILayout up = down;
            up.anchorMin.y = 1.0f - down.anchorMax.y;
            up.anchorMax.y = 1.0f - down.anchorMin.y;
            up.pivot.y = 1.0f - down.pivot.y;
            up.anchoredPos.y = -down.anchoredPos.y;
            list->SetLayout(up);
            const UIRect above = list->GetCanvasBounds(canvasSize);
            const float overBelow = below.max.y - canvasSize.y;
            const float overAbove = (std::max)(-above.min.y, 0.0f);
            if (overAbove > overBelow) {
                list->SetLayout(down);
            }
        }

        // 今の値の行が窓の真ん中に来るよう動かす
        if (view && content) {
            Vector2 normalized = scroll->GetNormalizedPosition();
            normalized.y = 0.0f;
            const UIDropdownItemComponent* const row = FindRow(value_);
            const RectTransformComponent* const rowRect = row ? row->GetRectTransform() : nullptr;
            const UIRect viewBounds = view->GetCanvasBounds(canvasSize);
            const UIRect contentBounds = content->GetCanvasBounds(canvasSize);
            const float scrollable = contentBounds.Size().y - viewBounds.Size().y;
            if (rowRect && scrollable > 0.0f) {
                const UIRect rowBounds = rowRect->GetCanvasBounds(canvasSize);
                const float rowCenter = (rowBounds.min.y + rowBounds.max.y) * 0.5f - contentBounds.min.y;
                normalized.y = std::clamp((rowCenter - viewBounds.Size().y * 0.5f) / scrollable, 0.0f, 1.0f);
            }
            scroll->StopMovement();
            scroll->SetNormalizedPosition(normalized);
            scroll->SyncScrollbars();
        }
    }

    // ===== IUIInteractable =====

    RectTransformComponent* UIDropdownComponent::GetOpenPopup() const
    {
        return open_ ? openList_.Get() : nullptr;
    }

    IUIInteractable* UIDropdownComponent::GetPopupInitialFocus() const
    {
        if (UIDropdownItemComponent* const row = FindRow(value_)) {
            return row;
        }
        return FindRow(0);
    }

    bool UIDropdownComponent::IsPopupFocusTarget(const IUIInteractable& candidate) const
    {
        const auto* const item = dynamic_cast<const UIDropdownItemComponent*>(&candidate);
        return item && item->GetDropdown() == this;
    }

    void PlaceOpenDropdownLists(GameObjectManager& manager)
    {
        if (!gPlacementRequested.exchange(false)) {
            return;
        }
        manager.ForEachComponent<UIDropdownComponent>([](UIDropdownComponent& dropdown) {
            dropdown.PlaceListIfPending();
            });
    }
}
