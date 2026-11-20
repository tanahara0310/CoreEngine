#include "pch.h"
#include "Editor/Scene/ObjectEditing.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "GameObject/Component/Environment/FogVolumeComponent.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Particle/Gpu/GpuParticleSystemComponent.h"
#include "Particle/ParticleSystemComponent.h"
#include "Scene/PrefabSystem.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIButtonComponent.h"
#include "UI/UIContentSizeFitterComponent.h"
#include "UI/UIDropdownComponent.h"
#include "UI/UIDropdownItemComponent.h"
#include "UI/UIImageComponent.h"
#include "UI/UIInputFieldComponent.h"
#include "UI/UILayoutGroupComponent.h"
#include "UI/UIMaskComponent.h"
#include "UI/UIScrollViewComponent.h"
#include "UI/UISliderComponent.h"
#include "UI/UITextComponent.h"
#include "Utility/Logger/Logger.h"
#include "Utility/String/FailWith.h"

#include <cctype>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace CoreEngine::ObjectEditing
{
    namespace
    {
        /// 空のオブジェクトの名前
        constexpr const char* kEmptyObjectName = "GameObject";

        /// パーティクルのオブジェクトの名前
        constexpr const char* kParticleObjectName = "Particle System";

        /// GPU パーティクルのオブジェクトの名前
        constexpr const char* kGpuParticleObjectName = "GPU Particle System";

        /// 局所の霧のオブジェクトの名前
        constexpr const char* kFogVolumeObjectName = "Fog Volume";

        /// UI テキストのオブジェクトの名前
        constexpr const char* kTextObjectName = "Text";

        /// UI 画像のオブジェクトの名前
        constexpr const char* kImageObjectName = "Image";

        /// 作った UI テキストの文字列
        constexpr const char* kNewTextString = "新しいテキスト";

        /// UI ボタンのオブジェクトの名前
        constexpr const char* kButtonObjectName = "Button";

        /// UI パネルのオブジェクトの名前
        constexpr const char* kPanelObjectName = "Panel";

        /// 作った UI ボタンの文字列
        constexpr const char* kNewButtonString = "ボタン";

        /// 作った UI ボタンの大きさ（px）
        const Vector2 kNewButtonSize = { 160.0f, 40.0f };

        /// 作った UI ボタンの文字の色
        const Vector4 kNewButtonTextColor = { 0.2f, 0.2f, 0.2f, 1.0f };

        /// 作った UI パネルの色
        const Vector4 kNewPanelColor = { 1.0f, 1.0f, 1.0f, 0.39f };

        /// スクロールの一覧の各オブジェクトの名前
        constexpr const char* kScrollViewObjectName = "Scroll View";
        constexpr const char* kScrollContentObjectName = "Content";
        constexpr const char* kScrollItemObjectName = "Item";
        constexpr const char* kScrollbarObjectName = "Scrollbar";
        constexpr const char* kScrollbarHandleObjectName = "Handle";

        /// 作ったスクロールの一覧の窓の大きさ（px）と色
        const Vector2 kNewScrollViewSize = { 400.0f, 300.0f };
        const Vector4 kNewScrollViewColor = { 0.12f, 0.13f, 0.16f, 0.9f };

        /// 中身の内側の余白（左・上・右・下。右はスクロールバーの分を空ける）と項目の間隔（px）
        const Vector4 kNewScrollContentPadding = { 8.0f, 8.0f, 28.0f, 8.0f };
        constexpr float kNewScrollContentSpacing = 4.0f;

        /// 最初に入れておく項目の数
        constexpr int kNewScrollItemCount = 3;

        /// スクロールバーの幅・つまみの長さ・窓の端からの間（px）と色
        constexpr float kScrollbarWidth = 16.0f;
        constexpr float kScrollbarHandleLength = 48.0f;
        constexpr float kScrollbarInset = 4.0f;
        const Vector4 kNewScrollbarColor = { 0.08f, 0.08f, 0.1f, 1.0f };

        /// ドロップダウンの各オブジェクトの名前
        constexpr const char* kDropdownObjectName = "Dropdown";
        constexpr const char* kDropdownLabelObjectName = "Label";
        constexpr const char* kDropdownArrowObjectName = "Arrow";
        constexpr const char* kDropdownTemplateObjectName = "Template";
        constexpr const char* kDropdownItemObjectName = "Item";
        constexpr const char* kDropdownItemCheckmarkObjectName = "Item Checkmark";
        constexpr const char* kDropdownItemLabelObjectName = "Item Label";

        /// 作ったドロップダウンの選択肢
        constexpr const char* kNewDropdownOptions[] = { "選択肢 A", "選択肢 B", "選択肢 C" };

        /// 作ったドロップダウンの押す所の大きさ・一覧の高さ・行の高さ（px）
        const Vector2 kNewDropdownSize = { 240.0f, 40.0f };
        constexpr float kNewDropdownListHeight = 200.0f;
        constexpr float kNewDropdownItemHeight = 36.0f;

        /// 作ったドロップダウンの文字の大きさ（押す所・矢印・行・印）
        constexpr float kNewDropdownFontSize = 24.0f;
        constexpr float kNewDropdownArrowFontSize = 16.0f;
        constexpr float kNewDropdownItemFontSize = 22.0f;

        /// 作ったドロップダウンの一覧の中身の余白（左・上・右・下。右はスクロールバーの分を空ける）
        const Vector4 kNewDropdownContentPadding = { 4.0f, 4.0f, 20.0f, 4.0f };

        /// 作ったドロップダウンの一覧のスクロールバーの幅と色
        constexpr float kDropdownScrollbarWidth = 12.0f;
        const Vector4 kNewDropdownScrollbarColor = { 0.82f, 0.83f, 0.86f, 1.0f };
        const Vector4 kNewDropdownHandleColor = { 0.5f, 0.52f, 0.58f, 1.0f };
        const Vector4 kNewDropdownHandleHoveredColor = { 0.4f, 0.42f, 0.48f, 1.0f };
        const Vector4 kNewDropdownHandlePressedColor = { 0.32f, 0.34f, 0.4f, 1.0f };

        /// 入力欄の各オブジェクトの名前
        constexpr const char* kInputFieldObjectName = "InputField";
        constexpr const char* kInputTextAreaObjectName = "Text Area";
        constexpr const char* kInputPlaceholderObjectName = "Placeholder";
        constexpr const char* kInputTextObjectName = "Text";

        /// 作った入力欄の置き文字
        constexpr const char* kNewInputPlaceholder = "文字を入力…";

        /// 作った入力欄の大きさ・文字の場所の枠からの間（左・上・右・下）・文字の大きさ（px）
        const Vector2 kNewInputFieldSize = { 320.0f, 48.0f };
        const Vector4 kNewInputTextInset = { 12.0f, 6.0f, 12.0f, 6.0f };
        constexpr float kNewInputFontSize = 24.0f;

        /// 作った入力欄の置き文字の色
        const Vector4 kNewInputPlaceholderColor = { 0.55f, 0.55f, 0.58f, 1.0f };

        /// 子孫をたどる深さの上限
        constexpr int kMaxHierarchyDepth = 64;

        /// UI を複製したときにずらす量（px）
        constexpr float kUIDuplicateOffset = 10.0f;

        /// @brief オブジェクトを作り直すための控え
        struct Snapshot
        {
            std::string name;
            std::string serializeKey;
            ObjectId id;
            json state;
            Reflection::AssetRefValue prefab;
        };

        /// @brief 今の状態を控える
        Snapshot Capture(const GameObject& object)
        {
            Snapshot snapshot;
            snapshot.name = object.GetName();
            snapshot.serializeKey = object.GetSerializeKey();
            snapshot.id = object.GetObjectId();
            snapshot.state = object.Serialize();
            if (object.IsPrefabInstance()) {
                snapshot.prefab = object.GetPrefab().GetValue();
            }
            return snapshot;
        }

        /// @brief 同じ名前のオブジェクトがあるか
        bool HasObjectNamed(const GameObjectManager& manager, const std::string& name)
        {
            for (const auto& object : manager.GetAllObjects()) {
                if (object && !object->IsMarkedForDestroy() && object->GetName() == name) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 「名前 (n)」の形なら、(n) を除いた名前を返す
        std::string StripCopySuffix(const std::string& name)
        {
            if (name.size() < 4 || name.back() != ')') {
                return name;
            }
            const std::size_t open = name.rfind(" (");
            if (open == std::string::npos || open + 3 >= name.size()) {
                return name;
            }
            for (std::size_t i = open + 2; i + 1 < name.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                    return name;
                }
            }
            return name.substr(0, open);
        }

        /// @brief 使われていない「名前 (n)」を作る
        std::string MakeCopyName(const GameObjectManager& manager, const std::string& sourceName)
        {
            const std::string baseName = StripCopySuffix(sourceName);
            for (int index = 1;; ++index) {
                std::string candidate = baseName + " (" + std::to_string(index) + ")";
                if (!HasObjectNamed(manager, candidate)) {
                    return candidate;
                }
            }
        }

        /// @brief 控えからオブジェクトを作ってシーンへ置く
        /// @param keepIdentity 控えた ID と保存キーで置くか（消したものを戻すとき）。false なら新しい保存キーを付ける
        GameObject* Recreate(GameObjectManager& manager, const Snapshot& snapshot, const std::string& name,
                             bool keepIdentity)
        {
            const std::string key = keepIdentity ? snapshot.serializeKey : MakeNewObjectKey(name);
            GameObject* object = nullptr;
            if (!snapshot.prefab.guid.empty() || !snapshot.prefab.path.empty()) {
                object = PrefabSystem::Instantiate(manager, snapshot.prefab, name, key);
            } else {
                auto owned = std::make_unique<GameObject>();
                owned->SetSerializeKey(key);
                owned->SetName(name);
                object = manager.AddObject(std::move(owned));
            }
            if (!object) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "ObjectEditing: \"{}\" を作り直せませんでした", name);
                return nullptr;
            }

            if (keepIdentity) {
                object->SetSerializeKey(snapshot.serializeKey);
                manager.AssignObjectId(*object, snapshot.id);
            }
            object->Deserialize(snapshot.state);
            object->SetName(name);
            manager.InvalidateReferences();
            return object;
        }

        /// @brief 後始末を呼んでから、削除の印を付ける
        void DestroyObject(const Context& context, GameObject& object)
        {
            if (context.beforeDestroy) {
                context.beforeDestroy(object);
            }
            object.Destroy();
            context.manager->InvalidateReferences();
        }

        /// @brief 今のシーンで、選択を外してからオブジェクトに削除の印を付ける
        void DestroyInScene(ObjectId id)
        {
            GameObjectManager* const manager = Editor::SceneAccess::Objects();
            GameObject* const target = manager ? manager->FindObject(id) : nullptr;
            if (!target) {
                return;
            }
            Editor::SceneAccess::Deselect(*target);
            target->Destroy();
            manager->InvalidateReferences();
        }

        /// @brief 今のシーンに、控えから同じ ID と保存キーでオブジェクトを作り直す
        void RecreateInScene(const Snapshot& snapshot)
        {
            if (GameObjectManager* const manager = Editor::SceneAccess::Objects()) {
                Recreate(*manager, snapshot, snapshot.name, true);
            }
        }

        /// @brief 作ったオブジェクトを、Undo で消し Redo で同じ ID のまま作り直すコマンドを積む
        /// @details Undo で消す直前の状態を控え、Redo はその状態で作り直す。
        void PushCreateCommand(const GameObject& object, std::string label)
        {
            const ObjectId id = object.GetObjectId();
            auto snapshot = std::make_shared<std::optional<Snapshot>>();
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [id, snapshot] {
                    if (const GameObject* const target = Editor::SceneAccess::FindObject(id)) {
                        *snapshot = Capture(*target);
                    }
                    DestroyInScene(id);
                },
                [snapshot] {
                    if (snapshot->has_value()) {
                        RecreateInScene(**snapshot);
                    }
                },
                true, true));
        }

        /// @brief オブジェクトを消し、Undo で同じ ID・保存キー・値のまま作り直すコマンドを積む
        void PushDeleteCommand(const Context& context, GameObject& object)
        {
            Snapshot snapshot = Capture(object);
            const ObjectId id = snapshot.id;
            const std::string label = snapshot.name + " を削除";
            DestroyObject(context, object);

            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                label,
                [snapshot = std::move(snapshot)] { RecreateInScene(snapshot); },
                [id] { DestroyInScene(id); },
                true, true));
        }

        /// @brief オブジェクトの子孫（Transform と UI トランスフォームの子）を、子から先の順に集める
        void CollectDescendants(const GameObjectManager& manager, const GameObject& object,
                                std::vector<GameObject*>& out, int depth = 0)
        {
            const TransformComponent* const transform = object.GetComponent<TransformComponent>();
            const RectTransformComponent* const rect = object.GetComponent<RectTransformComponent>();
            if ((!transform && !rect) || depth >= kMaxHierarchyDepth) {
                return;
            }
            for (const auto& candidate : manager.GetAllObjects()) {
                if (!candidate || candidate->IsMarkedForDestroy() || candidate.get() == &object) {
                    continue;
                }
                const TransformComponent* const child = candidate->GetComponent<TransformComponent>();
                const RectTransformComponent* const childRect = candidate->GetComponent<RectTransformComponent>();
                const bool isChild = (transform && child && child->GetParent() == transform)
                    || (rect && childRect && childRect->GetParent() == rect);
                if (isChild) {
                    CollectDescendants(manager, *candidate, out, depth + 1);
                    out.push_back(candidate.get());
                }
            }
        }

        /// @brief 同じ名前があれば「名前 (n)」に、無ければそのままの名前を返す
        std::string MakeUniqueName(const GameObjectManager& manager, const std::string& baseName)
        {
            return HasObjectNamed(manager, baseName) ? MakeCopyName(manager, baseName) : baseName;
        }

        /// @brief 名前のとおりのオブジェクトをシーンへ置き、コンポーネントを付けて「名前 を作る」のコマンドを積む
        /// @param attach エディタが作るコンポーネントとして付ける処理（GameObject& を受ける）
        /// @return 作ったオブジェクト（作れなければ nullptr）
        template <class Attach>
        GameObject* CreateObject(const Context& context, const std::string& name, Attach&& attach)
        {
            GameObjectManager& manager = *context.manager;
            auto owned = std::make_unique<GameObject>();
            owned->SetSerializeKey(MakeNewObjectKey(name));
            owned->SetName(name);
            GameObject* const object = manager.AddObject(std::move(owned));
            if (!object) {
                return nullptr;
            }

            // エディタが作るオブジェクトのコンポーネントとして付ける
            {
                ComponentHost::DataAttachScope dataScope(*object);
                attach(*object);
            }
            manager.InvalidateReferences();

            PushCreateCommand(*object, name + " を作る");
            return object;
        }

        /// @brief トランスフォームを付けて位置を置く
        void AddTransform(GameObject& object, const Vector3& position)
        {
            if (TransformComponent* const transform = object.AddComponent<TransformComponent>()) {
                transform->SetTranslate(position);
            }
        }

        /// @brief 作ったオブジェクトをログへ出す
        /// @param kind 何のオブジェクトか（「空のオブジェクト」など）
        void LogCreated(const char* kind, const GameObject& object)
        {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "ObjectEditing: {} \"{}\" を作りました", kind, object.GetName());
        }

        /// @brief UI トランスフォームを親いっぱいに伸ばす
        void StretchToParent(RectTransformComponent& rect)
        {
            rect.SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
            rect.SetSize({ 0.0f, 0.0f });
            rect.SetAnchoredPosition({ 0.0f, 0.0f });
        }

        /// @brief 画像とボタンを持つオブジェクトと、その子の文字のオブジェクトを作る
        /// @param parent ボタンの親（nullptr なら親なし）
        GameObject* CreateButtonObjects(const Context& context, const std::string& name,
                                        RectTransformComponent* parent, const std::string& label)
        {
            GameObject* const button = CreateObject(context, name, [parent](GameObject& created) {
                if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                    if (parent) {
                        rect->SetParent(parent, false);
                    }
                    rect->SetSize(kNewButtonSize);
                }
                created.AddComponent<UIImageComponent>();
                created.AddComponent<UIButtonComponent>();
            });
            if (!button) {
                return nullptr;
            }

            RectTransformComponent* const buttonRect = button->GetComponent<RectTransformComponent>();
            CreateObject(context, MakeUniqueName(*context.manager, kTextObjectName),
                [buttonRect, &label](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(buttonRect, false);
                        StretchToParent(*rect);
                    }
                    if (UITextComponent* const text = created.AddComponent<UITextComponent>()) {
                        text->SetFieldAutoFit(false);
                        text->SetText(label);
                        text->SetAlign(TextAlignH::Center, TextAlignV::Middle);
                        text->SetColor(kNewButtonTextColor);
                    }
                });
            return button;
        }

        /// @brief 画像とボタンを持つオブジェクトと、その子の文字のオブジェクトを作る（1 回の Undo で両方消える）
        GameObject* CreateButton(const Context& context)
        {
            const std::string name = MakeUniqueName(*context.manager, kButtonObjectName);
            Editor::BatchScope batch(name + " を作る");

            GameObject* const button = CreateButtonObjects(context, name, nullptr, kNewButtonString);
            if (button) {
                LogCreated("UI のボタン", *button);
            }
            return button;
        }

        /// @brief マスクした窓・縦に並べる中身・項目 3 つ・縦のスクロールバーを作る（1 回の Undo で全部消える）
        GameObject* CreateScrollView(const Context& context)
        {
            const std::string name = MakeUniqueName(*context.manager, kScrollViewObjectName);
            Editor::BatchScope batch(name + " を作る");

            GameObject* const view = CreateObject(context, name, [](GameObject& created) {
                if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                    rect->SetSize(kNewScrollViewSize);
                }
                if (UIImageComponent* const image = created.AddComponent<UIImageComponent>()) {
                    image->SetColor(kNewScrollViewColor);
                }
                created.AddComponent<UIMaskComponent>();
                created.AddComponent<UIScrollViewComponent>();
            });
            if (!view) {
                return nullptr;
            }
            RectTransformComponent* const viewRect = view->GetComponent<RectTransformComponent>();

            // 中身は窓の上辺に付けて横いっぱいに伸ばし、高さは並べた項目に合わせる
            GameObject* const content = CreateObject(context, MakeUniqueName(*context.manager, kScrollContentObjectName),
                [viewRect](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(viewRect, false);
                        rect->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 0.0f });
                        rect->SetPivot({ 0.5f, 0.0f });
                        rect->SetAnchoredPosition({ 0.0f, 0.0f });
                        rect->SetSize({ 0.0f, 0.0f });
                    }
                    if (UILayoutGroupComponent* const group = created.AddComponent<UILayoutGroupComponent>()) {
                        group->SetLayoutType(UILayoutType::Vertical);
                        group->SetPadding(kNewScrollContentPadding);
                        group->SetSpacing(kNewScrollContentSpacing);
                        group->SetControlChildSize(1, false);
                        group->SetForceExpand(1, false);
                    }
                    if (UIContentSizeFitterComponent* const fitter = created.AddComponent<UIContentSizeFitterComponent>()) {
                        fitter->SetHorizontalFit(UIFitMode::Unconstrained);
                        fitter->SetVerticalFit(UIFitMode::PreferredSize);
                    }
                });
            RectTransformComponent* const contentRect = content ? content->GetComponent<RectTransformComponent>() : nullptr;
            for (int i = 1; contentRect && i <= kNewScrollItemCount; ++i) {
                CreateButtonObjects(context, MakeUniqueName(*context.manager, kScrollItemObjectName),
                    contentRect, std::format("項目 {}", i));
            }

            // スクロールバーは窓の右端に、つまみの半分ずつ上下を空けて置く
            GameObject* const bar = CreateObject(context, MakeUniqueName(*context.manager, kScrollbarObjectName),
                [viewRect](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(viewRect, false);
                        rect->SetAnchors({ 1.0f, 0.0f }, { 1.0f, 1.0f });
                        rect->SetPivot({ 1.0f, 0.5f });
                        rect->SetAnchoredPosition({ -kScrollbarInset, 0.0f });
                        rect->SetSize({ kScrollbarWidth, -(kScrollbarHandleLength + kScrollbarInset * 2.0f) });
                    }
                    if (UIImageComponent* const image = created.AddComponent<UIImageComponent>()) {
                        image->SetColor(kNewScrollbarColor);
                    }
                    if (UISliderComponent* const slider = created.AddComponent<UISliderComponent>()) {
                        slider->SetDirection(UISliderComponent::Direction::TopToBottom);
                        slider->SetValue(0.0f);
                    }
                });
            RectTransformComponent* const barRect = bar ? bar->GetComponent<RectTransformComponent>() : nullptr;
            GameObject* const handle = barRect
                ? CreateObject(context, MakeUniqueName(*context.manager, kScrollbarHandleObjectName),
                    [barRect](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(barRect, false);
                            rect->SetSize({ kScrollbarWidth, kScrollbarHandleLength });
                        }
                        created.AddComponent<UIImageComponent>();
                    })
                : nullptr;

            if (UIScrollViewComponent* const scroll = view->GetComponent<UIScrollViewComponent>()) {
                scroll->SetContent(contentRect);
                if (UISliderComponent* const slider = bar ? bar->GetComponent<UISliderComponent>() : nullptr) {
                    if (handle) {
                        slider->SetHandle(handle->GetComponent<RectTransformComponent>());
                    }
                    // つまみを繋ぐ前にスライダーが塗った帯の色を戻す
                    if (UIImageComponent* const track = bar->GetComponent<UIImageComponent>()) {
                        track->SetColor(kNewScrollbarColor);
                    }
                    scroll->SetVerticalScrollbar(slider);
                }
            }
            LogCreated("UI のスクロールの一覧", *view);
            return view;
        }

        /// @brief 親の中で左右（上下）を空けて伸ばす
        /// @param inset 親の端からの間（左・上・右・下。px）
        void StretchWithInset(RectTransformComponent& rect, const Vector4& inset)
        {
            rect.SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
            rect.SetSize({ -(inset.x + inset.z), -(inset.y + inset.w) });
            rect.SetAnchoredPosition({ (inset.x - inset.z) * 0.5f, (inset.y - inset.w) * 0.5f });
        }

        /// @brief 文字を 1 つ持つ UI のオブジェクトを作る
        /// @param place 文字の UI トランスフォームを置く処理（RectTransformComponent& を受ける）
        template <class Place>
        GameObject* CreateTextObject(const Context& context, const std::string& baseName, RectTransformComponent* parent,
                                     const std::string& label, float fontSize, TextAlignH alignH, Place&& place)
        {
            return CreateObject(context, MakeUniqueName(*context.manager, baseName),
                [&](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(parent, false);
                        place(*rect);
                    }
                    if (UITextComponent* const text = created.AddComponent<UITextComponent>()) {
                        text->SetFieldAutoFit(false);
                        text->SetText(label);
                        text->SetFontSize(fontSize);
                        text->SetAlign(alignH, TextAlignV::Middle);
                        text->SetColor(kNewButtonTextColor);
                    }
                });
        }

        /// @brief 押す所・今の値の文字・矢印と、隠した一覧のひな形（行のひな形とスクロールバー）を作る（1 回の Undo で全部消える）
        GameObject* CreateDropdown(const Context& context)
        {
            const std::string name = MakeUniqueName(*context.manager, kDropdownObjectName);
            Editor::BatchScope batch(name + " を作る");

            GameObject* const dropdown = CreateObject(context, name, [](GameObject& created) {
                if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                    rect->SetSize(kNewDropdownSize);
                }
                created.AddComponent<UIImageComponent>();
                if (UIDropdownComponent* const component = created.AddComponent<UIDropdownComponent>()) {
                    component->SetOptions({ std::begin(kNewDropdownOptions), std::end(kNewDropdownOptions) });
                }
            });
            if (!dropdown) {
                return nullptr;
            }
            RectTransformComponent* const dropdownRect = dropdown->GetComponent<RectTransformComponent>();

            // 今の値の文字は左を詰め、右は矢印の分を空ける
            GameObject* const label = CreateTextObject(context, kDropdownLabelObjectName, dropdownRect,
                kNewDropdownOptions[0], kNewDropdownFontSize, TextAlignH::Left,
                [](RectTransformComponent& rect) { StretchWithInset(rect, { 12.0f, 0.0f, 32.0f, 0.0f }); });
            CreateTextObject(context, kDropdownArrowObjectName, dropdownRect, "▼", kNewDropdownArrowFontSize, TextAlignH::Center,
                [](RectTransformComponent& rect) {
                    rect.SetAnchors({ 1.0f, 0.5f }, { 1.0f, 0.5f });
                    rect.SetAnchoredPosition({ -18.0f, 0.0f });
                    rect.SetSize({ 24.0f, 24.0f });
                });

            // 一覧は押す所の下辺に付けて横いっぱいに伸ばす
            GameObject* const list = CreateObject(context, MakeUniqueName(*context.manager, kDropdownTemplateObjectName),
                [dropdownRect](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(dropdownRect, false);
                        rect->SetAnchors({ 0.0f, 1.0f }, { 1.0f, 1.0f });
                        rect->SetPivot({ 0.5f, 0.0f });
                        rect->SetAnchoredPosition({ 0.0f, 2.0f });
                        rect->SetSize({ 0.0f, kNewDropdownListHeight });
                    }
                    created.AddComponent<UIImageComponent>();
                    created.AddComponent<UIMaskComponent>();
                    created.AddComponent<UIScrollViewComponent>();
                });
            RectTransformComponent* const listRect = list ? list->GetComponent<RectTransformComponent>() : nullptr;

            GameObject* const content = listRect
                ? CreateObject(context, MakeUniqueName(*context.manager, kScrollContentObjectName),
                    [listRect](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(listRect, false);
                            rect->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 0.0f });
                            rect->SetPivot({ 0.5f, 0.0f });
                            rect->SetAnchoredPosition({ 0.0f, 0.0f });
                            rect->SetSize({ 0.0f, 0.0f });
                        }
                        if (UILayoutGroupComponent* const group = created.AddComponent<UILayoutGroupComponent>()) {
                            group->SetLayoutType(UILayoutType::Vertical);
                            group->SetPadding(kNewDropdownContentPadding);
                            group->SetSpacing(0.0f);
                            group->SetControlChildSize(1, false);
                            group->SetForceExpand(1, false);
                        }
                        if (UIContentSizeFitterComponent* const fitter = created.AddComponent<UIContentSizeFitterComponent>()) {
                            fitter->SetHorizontalFit(UIFitMode::Unconstrained);
                            fitter->SetVerticalFit(UIFitMode::PreferredSize);
                        }
                    })
                : nullptr;
            RectTransformComponent* const contentRect = content ? content->GetComponent<RectTransformComponent>() : nullptr;

            // 行のひな形：背景の画像・選んでいる行の印・選択肢の文字
            GameObject* const item = contentRect
                ? CreateObject(context, MakeUniqueName(*context.manager, kDropdownItemObjectName),
                    [contentRect](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(contentRect, false);
                            rect->SetSize({ kNewDropdownSize.x, kNewDropdownItemHeight });
                        }
                        created.AddComponent<UIImageComponent>();
                        created.AddComponent<UIDropdownItemComponent>();
                    })
                : nullptr;
            RectTransformComponent* const itemRect = item ? item->GetComponent<RectTransformComponent>() : nullptr;
            GameObject* const checkmark = itemRect
                ? CreateTextObject(context, kDropdownItemCheckmarkObjectName, itemRect, "✓", kNewDropdownItemFontSize,
                    TextAlignH::Center, [](RectTransformComponent& rect) {
                        rect.SetAnchors({ 0.0f, 0.5f }, { 0.0f, 0.5f });
                        rect.SetAnchoredPosition({ 16.0f, 0.0f });
                        rect.SetSize({ 24.0f, 24.0f });
                    })
                : nullptr;
            GameObject* const itemLabel = itemRect
                ? CreateTextObject(context, kDropdownItemLabelObjectName, itemRect, kNewDropdownOptions[0],
                    kNewDropdownItemFontSize, TextAlignH::Left,
                    [](RectTransformComponent& rect) { StretchWithInset(rect, { 32.0f, 0.0f, 8.0f, 0.0f }); })
                : nullptr;

            // スクロールバーは一覧の右端に、つまみの半分ずつ上下を空けて置く。つまみは行と同じ高さ
            GameObject* const bar = listRect
                ? CreateObject(context, MakeUniqueName(*context.manager, kScrollbarObjectName),
                    [listRect](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(listRect, false);
                            rect->SetAnchors({ 1.0f, 0.0f }, { 1.0f, 1.0f });
                            rect->SetPivot({ 1.0f, 0.5f });
                            rect->SetAnchoredPosition({ -kScrollbarInset, 0.0f });
                            rect->SetSize({ kDropdownScrollbarWidth, -(kNewDropdownItemHeight + kScrollbarInset * 2.0f) });
                        }
                        if (UIImageComponent* const image = created.AddComponent<UIImageComponent>()) {
                            image->SetColor(kNewDropdownScrollbarColor);
                        }
                        if (UISliderComponent* const slider = created.AddComponent<UISliderComponent>()) {
                            slider->SetDirection(UISliderComponent::Direction::TopToBottom);
                            slider->SetValue(0.0f);
                        }
                    })
                : nullptr;
            RectTransformComponent* const barRect = bar ? bar->GetComponent<RectTransformComponent>() : nullptr;
            GameObject* const handle = barRect
                ? CreateObject(context, MakeUniqueName(*context.manager, kScrollbarHandleObjectName),
                    [barRect](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(barRect, false);
                            rect->SetSize({ kDropdownScrollbarWidth, kNewDropdownItemHeight });
                        }
                        if (UIImageComponent* const image = created.AddComponent<UIImageComponent>()) {
                            image->SetColor(kNewDropdownHandleColor);
                        }
                    })
                : nullptr;

            // 繋ぐ
            if (UIScrollViewComponent* const scroll = list ? list->GetComponent<UIScrollViewComponent>() : nullptr) {
                scroll->SetContent(contentRect);
                if (UISliderComponent* const slider = bar ? bar->GetComponent<UISliderComponent>() : nullptr) {
                    if (handle) {
                        slider->SetHandle(handle->GetComponent<RectTransformComponent>());
                    }
                    // スライダーはつまみを状態ごとの色で塗る
                    slider->SetStateColors(kNewDropdownHandleColor, kNewDropdownHandleHoveredColor,
                                           kNewDropdownHandlePressedColor);
                    // つまみを繋ぐ前にスライダーが塗った帯の色を戻す
                    if (UIImageComponent* const track = bar->GetComponent<UIImageComponent>()) {
                        track->SetColor(kNewDropdownScrollbarColor);
                    }
                    scroll->SetVerticalScrollbar(slider);
                }
            }
            UIDropdownItemComponent* const itemComponent = item ? item->GetComponent<UIDropdownItemComponent>() : nullptr;
            if (itemComponent && checkmark) {
                itemComponent->SetCheckmark(checkmark->GetComponent<RectTransformComponent>());
            }
            if (UIDropdownComponent* const component = dropdown->GetComponent<UIDropdownComponent>()) {
                component->SetCaptionText(label ? label->GetComponent<UITextComponent>() : nullptr);
                component->SetTemplate(listRect);
                component->SetItem(itemComponent, itemLabel ? itemLabel->GetComponent<UITextComponent>() : nullptr);
            }

            // 一覧のひな形は隠しておく（再生中に開くと出る）
            if (list) {
                list->SetActive(false);
            }
            LogCreated("UI のドロップダウン", *dropdown);
            return dropdown;
        }

        /// @brief 枠・文字の場所（マスク）・打った文字・置き文字を作る（1 回の Undo で全部消える）
        GameObject* CreateInputField(const Context& context)
        {
            const std::string name = MakeUniqueName(*context.manager, kInputFieldObjectName);
            Editor::BatchScope batch(name + " を作る");

            GameObject* const field = CreateObject(context, name, [](GameObject& created) {
                if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                    rect->SetSize(kNewInputFieldSize);
                }
                created.AddComponent<UIImageComponent>();
                created.AddComponent<UIInputFieldComponent>();
            });
            if (!field) {
                return nullptr;
            }
            RectTransformComponent* const fieldRect = field->GetComponent<RectTransformComponent>();

            // 文字の場所は枠の内側に伸ばし、はみ出した文字を切る
            GameObject* const area = CreateObject(context, MakeUniqueName(*context.manager, kInputTextAreaObjectName),
                [fieldRect](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        rect->SetParent(fieldRect, false);
                        StretchWithInset(*rect, kNewInputTextInset);
                    }
                    created.AddComponent<UIMaskComponent>();
                });
            RectTransformComponent* const areaRect = area ? area->GetComponent<RectTransformComponent>() : nullptr;

            // 置き文字と打った文字は、文字の場所の左の真ん中から右へ伸ばす
            const auto createLabel = [&](const char* baseName, const std::string& label, const Vector4& color) {
                return CreateObject(context, MakeUniqueName(*context.manager, baseName),
                    [&](GameObject& created) {
                        if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                            rect->SetParent(areaRect, false);
                            rect->SetAnchors({ 0.0f, 0.5f }, { 0.0f, 0.5f });
                            rect->SetPivot({ 0.0f, 0.5f });
                            rect->SetAnchoredPosition({ 0.0f, 0.0f });
                        }
                        if (UITextComponent* const text = created.AddComponent<UITextComponent>()) {
                            text->SetText(label);
                            text->SetFontSize(kNewInputFontSize);
                            text->SetAlign(TextAlignH::Left, TextAlignV::Middle);
                            text->SetColor(color);
                        }
                    });
                };
            GameObject* const placeholder = areaRect
                ? createLabel(kInputPlaceholderObjectName, kNewInputPlaceholder, kNewInputPlaceholderColor) : nullptr;
            GameObject* const text = areaRect
                ? createLabel(kInputTextObjectName, std::string(), kNewButtonTextColor) : nullptr;

            if (UIInputFieldComponent* const component = field->GetComponent<UIInputFieldComponent>()) {
                component->SetTextComponent(text ? text->GetComponent<UITextComponent>() : nullptr);
                component->SetPlaceholder(placeholder ? placeholder->GetComponent<UITextComponent>() : nullptr);
            }
            LogCreated("UI の入力欄", *field);
            return field;
        }

        /// @brief 親いっぱいに伸ばした半透明の画像を持つオブジェクトを作る
        GameObject* CreatePanel(const Context& context)
        {
            GameObject* const panel = CreateObject(context, MakeUniqueName(*context.manager, kPanelObjectName),
                [](GameObject& created) {
                    if (RectTransformComponent* const rect = created.AddComponent<RectTransformComponent>()) {
                        StretchToParent(*rect);
                    }
                    if (UIImageComponent* const image = created.AddComponent<UIImageComponent>()) {
                        image->SetColor(kNewPanelColor);
                    }
                });
            if (panel) {
                LogCreated("UI のパネル", *panel);
            }
            return panel;
        }
    }

    std::string MakeNewObjectKey(const std::string& name)
    {
        // 英数字と '-' を残し、ほかの文字の並びは '_' 1 つにする
        std::string base;
        for (const char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') {
                base.push_back(c);
            } else if (!base.empty() && base.back() != '_') {
                base.push_back('_');
            }
        }
        while (!base.empty() && base.back() == '_') {
            base.pop_back();
        }
        if (base.empty()) {
            base = "Object";
        }

        static std::mt19937_64 random{ std::random_device{}() };
        return std::format("{}_{:08x}", base, static_cast<std::uint32_t>(random()));
    }

    bool CanDuplicateOrDelete(const GameObject& object, std::string* reason)
    {
        if (object.IsMarkedForDestroy()) {
            return FailWith(reason, "削除の途中のオブジェクトです");
        }
        if (!object.IsSerializeEnabled()) {
            return FailWith(reason, "シーンに保存しないオブジェクトは扱えません");
        }
        for (const auto& component : object.GetAllComponents()) {
            if (component && component->IsAttachedByCode()) {
                return FailWith(reason, "コードが作ったオブジェクトは、エディタから複製・削除できません");
            }
        }
        return true;
    }

    GameObject* CreateEmpty(const Context& context, const Vector3& position)
    {
        GameObject* const object = CreateObject(context, MakeUniqueName(*context.manager, kEmptyObjectName),
            [&position](GameObject& created) { AddTransform(created, position); });
        if (object) {
            LogCreated("空のオブジェクト", *object);
        }
        return object;
    }

    GameObject* CreateNamed(const Context& context, const std::string& name, const Vector3& position)
    {
        return CreateObject(context, name, [&position](GameObject& created) { AddTransform(created, position); });
    }

    GameObject* InstantiatePrefab(const Context& context, const std::string& prefabPath, const std::string& name)
    {
        GameObjectManager& manager = *context.manager;
        GameObject* const object = PrefabSystem::Instantiate(
            manager, Reflection::AssetRefValue{ std::string(), prefabPath }, name, MakeNewObjectKey(name));
        if (!object) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectEditing: プレハブ {} を読めないので、\"{}\" を作れませんでした", prefabPath, name);
            return nullptr;
        }
        manager.InvalidateReferences();

        PushCreateCommand(*object, name + " を作る");
        return object;
    }

    GameObject* CreateParticle(const Context& context, ParticleKind kind, const Vector3& position)
    {
        const char* const baseName = (kind == ParticleKind::Gpu) ? kGpuParticleObjectName : kParticleObjectName;

        // トランスフォームを先頭に付ける
        GameObject* const object = CreateObject(context, MakeUniqueName(*context.manager, baseName),
            [kind, &position](GameObject& created) {
                AddTransform(created, position);
                if (kind == ParticleKind::Gpu) {
                    created.AddComponent<GpuParticleSystemComponent>();
                } else {
                    created.AddComponent<ParticleSystemComponent>();
                }
            });
        if (object) {
            LogCreated("パーティクルのオブジェクト", *object);
        }
        return object;
    }

    GameObject* CreateFogVolume(const Context& context, const Vector3& groundPosition)
    {
        // トランスフォームを先頭に付ける。形の底が置いた所に着くよう、中心を大きさの半分だけ上げる
        GameObject* const object = CreateObject(context, MakeUniqueName(*context.manager, kFogVolumeObjectName),
            [&groundPosition](GameObject& created) {
                TransformComponent* const transform = created.AddComponent<TransformComponent>();
                const FogVolumeComponent* const volume = created.AddComponent<FogVolumeComponent>();
                if (transform) {
                    const float halfHeight = volume ? volume->GetSize().y * 0.5f : 0.0f;
                    transform->SetTranslate(groundPosition + Vector3{ 0.0f, halfHeight, 0.0f });
                }
            });
        if (object) {
            LogCreated("局所の霧のオブジェクト", *object);
        }
        return object;
    }

    GameObject* CreateSprite(const Context& context, const std::string& baseName, const std::string& texturePath,
                             std::optional<float> pixelsPerUnit, const Vector3& position)
    {
        // トランスフォームを先頭に付ける
        GameObject* const object = CreateObject(context, MakeUniqueName(*context.manager, baseName),
            [&texturePath, pixelsPerUnit, &position](GameObject& created) {
                AddTransform(created, position);
                if (SpriteRendererComponent* const sprite = created.AddComponent<SpriteRendererComponent>()) {
                    if (pixelsPerUnit) {
                        sprite->SetPixelsPerUnit(*pixelsPerUnit);
                    }
                    sprite->SetTexture(texturePath);
                }
            });
        if (object) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "ObjectEditing: スプライトのオブジェクト \"{}\" を作りました（{}）", object->GetName(), texturePath);
        }
        return object;
    }

    GameObject* CreateUI(const Context& context, UIElementKind kind)
    {
        if (kind == UIElementKind::Button) {
            return CreateButton(context);
        }
        if (kind == UIElementKind::Panel) {
            return CreatePanel(context);
        }
        if (kind == UIElementKind::ScrollView) {
            return CreateScrollView(context);
        }
        if (kind == UIElementKind::Dropdown) {
            return CreateDropdown(context);
        }
        if (kind == UIElementKind::InputField) {
            return CreateInputField(context);
        }

        const char* const baseName = (kind == UIElementKind::Text) ? kTextObjectName : kImageObjectName;

        // UI トランスフォームを先頭に付ける
        GameObject* const object = CreateObject(context, MakeUniqueName(*context.manager, baseName),
            [kind](GameObject& created) {
                created.AddComponent<RectTransformComponent>();
                if (kind == UIElementKind::Text) {
                    if (UITextComponent* const text = created.AddComponent<UITextComponent>()) {
                        text->SetText(kNewTextString);
                    }
                } else {
                    created.AddComponent<UIImageComponent>();
                }
            });
        if (object) {
            LogCreated("UI のオブジェクト", *object);
        }
        return object;
    }

    bool CanSetUIParent(const GameObject& child, const GameObject* parent)
    {
        const RectTransformComponent* const rect = child.GetComponent<RectTransformComponent>();
        if (!rect || child.IsMarkedForDestroy()) {
            return false;
        }
        if (!parent) {
            return rect->GetParent() != nullptr;
        }
        if (parent == &child || parent->IsMarkedForDestroy()) {
            return false;
        }
        const RectTransformComponent* const parentRect = parent->GetComponent<RectTransformComponent>();
        if (!parentRect || rect->GetParent() == parentRect) {
            return false;
        }
        return !rect->IsSelfOrDescendant(parentRect);
    }

    bool SetUIParent(GameObject& child, GameObject* parent)
    {
        if (!CanSetUIParent(child, parent)) {
            return false;
        }
        RectTransformComponent* const rect = child.GetComponent<RectTransformComponent>();
        RectTransformComponent* const parentRect = parent ? parent->GetComponent<RectTransformComponent>() : nullptr;

        // 親と配置をまとめて控え、まとめて戻す
        struct ParentState
        {
            UILayout layout;
            Reflection::ObjectRefValue parent;
        };
        const auto capture = [](RectTransformComponent& target) {
            return ParentState{ target.GetLayout(), target.GetParentValue() };
        };
        const auto restore = [](RectTransformComponent& target, const ParentState& state) {
            target.SetParentValue(state.parent);
            target.SetLayout(state.layout);
        };

        ParentState before = capture(*rect);
        if (!rect->SetParent(parentRect, true)) {
            return false;
        }

        const std::string label = parent
            ? child.GetName() + " を " + parent->GetName() + " の子にする"
            : child.GetName() + " の親を外す";
        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::ComponentStateCommand<RectTransformComponent, ParentState>>(
                label, *rect, std::move(before), capture, restore));
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "ObjectEditing: {}", label);
        return true;
    }

    GameObject* Duplicate(const Context& context, const GameObject& source)
    {
        std::string reason;
        if (!CanDuplicateOrDelete(source, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectEditing: \"{}\" を複製できません: {}", source.GetName(), reason);
            return nullptr;
        }

        GameObjectManager& manager = *context.manager;
        const Snapshot snapshot = Capture(source);
        GameObject* const copy = Recreate(manager, snapshot, MakeCopyName(manager, source.GetName()), false);
        if (!copy) {
            return nullptr;
        }

        // 元と重ならないよう少しずらす
        if (ITransformSource* const transform = copy->GetComponent<ITransformSource>()) {
            Vector3 translate = transform->GetTranslate();
            translate.x += 1.0f;
            transform->SetTranslate(translate);
        } else if (RectTransformComponent* const rect = copy->GetComponent<RectTransformComponent>()) {
            const Vector2 position = rect->GetAnchoredPosition();
            rect->SetAnchoredPosition({ position.x + kUIDuplicateOffset, position.y + kUIDuplicateOffset });
        }

        PushCreateCommand(*copy, source.GetName() + " を複製");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: \"{}\" を \"{}\" として複製しました", source.GetName(), copy->GetName());
        return copy;
    }

    bool Delete(const Context& context, GameObject& object)
    {
        std::string reason;
        if (!CanDuplicateOrDelete(object, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectEditing: \"{}\" を削除できません: {}", object.GetName(), reason);
            return false;
        }

        const std::string name = object.GetName();
        PushDeleteCommand(context, object);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: \"{}\" を削除しました", name);
        return true;
    }

    bool DeleteWithDescendants(const Context& context, GameObject& object)
    {
        std::vector<GameObject*> targets;
        CollectDescendants(*context.manager, object, targets);
        targets.push_back(&object);

        for (const GameObject* const target : targets) {
            std::string reason;
            if (!CanDuplicateOrDelete(*target, &reason)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ObjectEditing: \"{}\" を削除できません（\"{}\": {}）", object.GetName(), target->GetName(), reason);
                return false;
            }
        }

        const std::string name = object.GetName();
        {
            Editor::BatchScope batch(name + " を削除");
            for (GameObject* const target : targets) {
                PushDeleteCommand(context, *target);
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: \"{}\" を子孫 {} 個と一緒に削除しました", name, targets.size() - 1);
        return true;
    }

    bool Rename(GameObject& object, const std::string& before, const std::string& after)
    {
        object.SetName(after);
        if (before == after) {
            return false;
        }

        Editor::EditorCommandStack::Get().Push(Editor::MakeObjectValueCommand(
            before + " の名前を " + after + " に変える", object.GetObjectId(), before, after,
            [](GameObject& target, const std::string& name) { target.SetName(name); }));
        return true;
    }

    bool SetActive(GameObject& object, bool active)
    {
        if (object.IsActive() == active) {
            return false;
        }
        object.SetActive(active);

        Editor::EditorCommandStack::Get().Push(Editor::MakeObjectValueCommand(
            object.GetName() + (active ? " を有効にする" : " を無効にする"), object.GetObjectId(), !active, active,
            [](GameObject& target, bool value) { target.SetActive(value); }));
        return true;
    }

    bool SetTag(GameObject& object, const std::string& tag)
    {
        const std::string before = object.GetTag();
        object.SetTag(tag);
        const std::string after = object.GetTag();
        if (before == after) {
            return false;
        }

        Editor::EditorCommandStack::Get().Push(Editor::MakeObjectValueCommand(
            object.GetName() + " のタグを " + after + " にする", object.GetObjectId(), before, after,
            [](GameObject& target, const std::string& value) { target.SetTag(value); }));
        return true;
    }
}

#endif // CORE_EDITOR
