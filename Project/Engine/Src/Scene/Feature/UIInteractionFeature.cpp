#include "pch.h"
#include "Scene/Feature/UIInteractionFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderPassType.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "UI/IUIInteractable.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIPointer.h"
#include "UI/UIScrollViewComponent.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <cmath>
#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// 決定に使うアクションの綴り
        constexpr const char* kConfirmActionId = "UIConfirm";

        /// 開いている一覧を閉じるアクションの綴り
        constexpr const char* kCancelActionId = "UICancel";

        /// 送りに使うアクションの綴り（`UINavigationDirection` と同じ並び）
        constexpr const char* kDirectionActionIds[] = {
            "UINavigateUp",
            "UINavigateDown",
            "UINavigateLeft",
            "UINavigateRight",
        };
        static_assert(std::size(kDirectionActionIds) == 4, "向きの数と綴りの数を合わせること");

        /// 押しっぱなしのとき、最初の 1 回から次までの待ち時間［秒］
        constexpr float kFirstRepeatDelay = 0.45f;

        /// そのあと送り続ける間隔［秒］
        constexpr float kRepeatInterval = 0.12f;

        /// ホイール 1 目盛りの値
        constexpr float kWheelPerNotch = 120.0f;
    }

    UIInteractionFeature* UIInteractionFeature::FindCurrent(EngineSystem* engine)
    {
        SceneManager* const sceneManager = engine ? engine->GetService<SceneManager>() : nullptr;
        auto* const scene = dynamic_cast<Scene*>(sceneManager ? sceneManager->GetCurrentScene() : nullptr);
        return scene ? scene->GetFeature<UIInteractionFeature>() : nullptr;
    }

    void UIInteractionFeature::Initialize(SceneContext& ctx)
    {
        (void)ctx;
        // 綴りが表に無いとフォーカス送りが黙って効かなくなるので、シーンを開いた時点で知らせる
        for (const char* const id : kDirectionActionIds) {
            if (InputActionFromString(id) == InputAction::Invalid) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "入力アクション \"{}\" がありません。キー・パッドでの UI の送りは効きません", id);
            }
        }
        if (InputActionFromString(kConfirmActionId) == InputAction::Invalid) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "入力アクション \"{}\" がありません。キー・パッドでの UI の決定は効きません",
                kConfirmActionId);
        }
    }

    void UIInteractionFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        // 編集中に効かせると、置いている最中のボタンが押されてしまう
        const bool playing = PlaybackStateManager::GetInstance().IsPlaying();
        if (!playing) {
            if (wasPlaying_) {
                ClearAll();
                RestoreGameContext(ctx);
                wasPlaying_ = false;
            }
            return;
        }
        wasPlaying_ = true;

        if (!ctx.gameObjectManager || !ctx.engine) {
            return;
        }
        // UIRenderer はサービスではなく RenderManager が持つ
        auto* const renderManager = ctx.engine->GetService<RenderManager>();
        auto* const renderer = renderManager
            ? renderManager->GetRendererAs<UIRenderer>(RenderPassType::UI)
            : nullptr;
        if (!renderer) {
            return;
        }

        const UIPointer& pointer = UIPointer::Get();
        const Vector2 canvasSize = renderer->GetScreenSize();
        const Vector2 onCanvas = pointer.ToCanvas(canvasSize);

        InputManager* const input = ctx.engine->GetService<InputManager>();
        const InputQuery* const query = input ? &input->GetQuery() : nullptr;
        const InputAction confirm = InputActionFromString(kConfirmActionId);
        const bool hasConfirm = query && confirm != InputAction::Invalid;
        const bool confirmDown = hasConfirm && query->IsActionTriggered(confirm);
        const bool confirmUp = hasConfirm && query->IsActionReleased(confirm);

        PopupScan popup;
        IUIInteractable* const under = ScanAndPick(ctx, canvasSize, onCanvas, pointer.IsOver(), popup);
        ChangePopup(ctx, canvasSize, popup);
        editing_ = popup.editing;

        // ---- 乗った・外れた ----
        if (under != hovered_) {
            if (hovered_) { hovered_->OnPointerExit(); }
            hovered_ = under;
            if (hovered_) { hovered_->OnPointerEnter(); }
        }

        // ---- 開いている一覧の外を押した・戻るを押した ----
        // 押した入力は一覧を閉じるだけに使い、下にある UI へは渡さない
        if (popupOwner_) {
            const InputAction cancel = query ? InputActionFromString(kCancelActionId) : InputAction::Invalid;
            const bool cancelDown = cancel != InputAction::Invalid && query->IsActionTriggered(cancel);
            const bool pressedOutside = pointer.IsPressed() && pointer.IsOver()
                && (!popupRoot_ || !popupRoot_->ContainsCanvasPoint(onCanvas, canvasSize));
            if (cancelDown || pressedOutside) {
                popupOwner_->ClosePopup();
            }
        }

        // ---- 打っている欄の外を押した・戻るを押した ----
        // 欄の外を押したら打った文字のまま終え、戻るなら打ち始める前の文字へ戻す
        if (editing_) {
            const InputAction cancel = query ? InputActionFromString(kCancelActionId) : InputAction::Invalid;
            const bool cancelDown = cancel != InputAction::Invalid && query->IsActionTriggered(cancel);
            const bool pressedOutside = pointer.IsPressedAnywhere() && !(pointer.IsPressed() && hovered_ == editing_);
            if (cancelDown) {
                editing_->EndTextEditing(true);
            } else if (pressedOutside) {
                editing_->EndTextEditing(false);
            }
        }

        // ---- 押した ----
        if (!pressed_ && pointer.IsPressed() && hovered_) {
            pressed_ = hovered_;
            pressedByKey_ = false;
            pressed_->OnPressBegin();
        }
        if (!pressed_ && focused_ && confirmDown) {
            pressed_ = focused_;
            pressedByKey_ = true;
            pressed_->OnPressBegin();
        }

        // ---- スクロール（ホイール・押して動かす） ----
        // ドラッグに切り替えたら押していたボタンを離すので、押したまま動かす処理より先に見る
        UIScrollViewComponent* const scrollUnder = PickScrollView(ctx, canvasSize, onCanvas, pointer.IsOver());
        UpdateScroll(scrollUnder, canvasSize, onCanvas, pointer, query);

        // ---- 押したまま動かしている ----
        // つまみを掴んで引きずる UI はここで位置を受け取る
        if (pressed_ && !pressedByKey_) {
            pressed_->OnDrag(onCanvas, canvasSize);
        }

        // ---- 離した ----
        if (pressed_) {
            // 押し始めた入力で離したかを見る（マウスで押して決定キーを離す、の取り違えを防ぐ）
            const bool released = pressedByKey_ ? confirmUp : pointer.IsReleased();
            if (released) {
                // 押し始めた相手の上で離したときだけクリックにする
                const IUIInteractable* const on = pressedByKey_ ? focused_ : hovered_;
                pressed_->OnPressEnd(pressed_ == on);
                pressed_ = nullptr;
            }
        }

        // ---- キー・パッドでの送り ----
        if (query) {
            UpdateNavigation(ctx, canvasSize, *query);
        }

        // ---- 場面 ----
        // 選んでいる相手がいる間・一覧が開いている間・文字を打っている間はメニューの操作にする。
        // これが無いと、決定ボタンでボタンを押した瞬間にキャラも跳ぶ
        if (input) {
            const bool ui = focused_ || popupOwner_ || (editing_ && editing_->IsEditingText());
            input->GetQuery().SetActiveContexts((ui ? InputContext::UI : InputContext::Game) | kEditorContext);
        }
    }

    void UIInteractionFeature::RestoreGameContext(SceneContext& ctx)
    {
        if (!ctx.engine) {
            return;
        }
        if (InputManager* const input = ctx.engine->GetService<InputManager>()) {
            input->GetQuery().SetActiveContexts(InputContext::Game | kEditorContext);
        }
    }

    IUIInteractable* UIInteractionFeature::ScanAndPick(SceneContext& ctx, const Vector2& canvasSize,
                                                      const Vector2& pointer, bool pointerOver,
                                                      PopupScan& outPopup)
    {
        Presence hovered;
        Presence focused;
        Presence pressed;
        hits_.clear();

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                // 親が隠れている UI は受け付けない
                RectTransformComponent* const rect = candidate.GetRectTransform();
                const bool accepts = candidate.AcceptsInput() && (!rect || rect->IsActiveInHierarchy());

                // 覚えている相手がまだ居るかをここで確かめる
                // （走査に出てこないものは、消えたか止まっている）
                const auto mark = [&](const IUIInteractable* known, Presence& presence) {
                    if (known == &candidate) {
                        presence.found = true;
                        presence.accepts = accepts;
                    }
                    };
                mark(hovered_, hovered);
                mark(focused_, focused);
                mark(pressed_, pressed);
                mark(popupOwner_, outPopup.previous);

                if (accepts) {
                    if (RectTransformComponent* const popupRoot = candidate.GetOpenPopup()) {
                        outPopup.owner = &candidate;
                        outPopup.root = popupRoot;
                    }
                    if (candidate.IsEditingText()) {
                        outPopup.editing = &candidate;
                    }
                }

                if (!accepts || !pointerOver) {
                    return;
                }
                if (!rect || !rect->ContainsCanvasPoint(pointer, canvasSize)) {
                    return;
                }
                hits_.push_back({ &candidate, rect, candidate.GetPointerSortOrder() });
            });

        ReleaseLost(hovered, focused, pressed);

        // 描画順が大きいほど手前。同じなら後から見つかった方を手前として扱う。
        // 一覧が開いていれば、一覧の中の物だけから選ぶ
        IUIInteractable* best = nullptr;
        int bestOrder = 0;
        for (const PointerHit& hit : hits_) {
            if (outPopup.root && !outPopup.root->IsSelfOrDescendant(hit.rect)) {
                continue;
            }
            if (!best || hit.order >= bestOrder) {
                best = hit.target;
                bestOrder = hit.order;
            }
        }
        return best;
    }

    void UIInteractionFeature::ChangePopup(SceneContext& ctx, const Vector2& canvasSize, const PopupScan& scan)
    {
        if (scan.owner == popupOwner_) {
            popupRoot_ = scan.root;
            if (popupOwner_ && focused_) {
                popupKeyboard_ = true;
            }
            return;
        }

        // 閉じた。一覧の中をキー・パッドで選んでいたなら、持ち主へフォーカスを戻す
        IUIInteractable* const closed = popupOwner_;
        const bool returnFocus = closed && popupKeyboard_ && scan.previous.found && scan.previous.accepts;
        popupOwner_ = scan.owner;
        popupRoot_ = scan.root;
        popupKeyboard_ = false;
        if (returnFocus && !popupOwner_) {
            SetFocus(closed);
        }

        // 開いた。キー・パッドで選んでいたなら、一覧の最初の相手へ移す
        if (popupOwner_ && focused_) {
            popupKeyboard_ = true;
            SetFocus(FindPopupInitialFocus(ctx, canvasSize));
        }
    }

    bool UIInteractionFeature::IsPopupFocusCandidate(const IUIInteractable& candidate) const
    {
        if (!popupOwner_) {
            return true;
        }
        const RectTransformComponent* const rect = candidate.GetRectTransform();
        return popupRoot_ && rect && popupRoot_->IsSelfOrDescendant(rect)
            && popupOwner_->IsPopupFocusTarget(candidate);
    }

    IUIInteractable* UIInteractionFeature::FindPopupInitialFocus(SceneContext& ctx, const Vector2& canvasSize) const
    {
        if (!popupOwner_) {
            return nullptr;
        }
        IUIInteractable* const preferred = popupOwner_->GetPopupInitialFocus();
        if (preferred && preferred->AcceptsInput() && IsPopupFocusCandidate(*preferred)) {
            return preferred;
        }
        return FindFirstFocusable(ctx, canvasSize);
    }

    void UIInteractionFeature::ReleaseLost(const Presence& hovered, const Presence& focused,
                                           const Presence& pressed)
    {
        // 消えた相手には通知を出せない。受け付けなくなっただけの相手には 1 回だけ知らせる
        if (pressed_ && !pressed.found) {
            pressed_ = nullptr;
        }
        else if (pressed_ && !pressed.accepts) {
            pressed_->OnPressEnd(false);
            pressed_ = nullptr;
        }

        if (hovered_ && !hovered.found) {
            hovered_ = nullptr;
        }
        else if (hovered_ && !hovered.accepts) {
            hovered_->OnPointerExit();
            hovered_ = nullptr;
        }

        if (focused_ && !focused.found) {
            focused_ = nullptr;
        }
        else if (focused_ && !focused.accepts) {
            focused_->OnFocusExit();
            focused_ = nullptr;
        }
    }

    UIScrollViewComponent* UIInteractionFeature::PickScrollView(SceneContext& ctx, const Vector2& canvasSize,
                                                                const Vector2& pointer, bool pointerOver)
    {
        UIScrollViewComponent* best = nullptr;
        int bestOrder = 0;
        bool targetFound = false;

        ctx.gameObjectManager->ForEachComponent<UIScrollViewComponent>([&](UIScrollViewComponent& view) {
            if (&view == scrollTarget_) {
                targetFound = true;
            }
            if (!pointerOver) {
                return;
            }
            const RectTransformComponent* const rect = view.GetRectTransform();
            if (!rect || !rect->IsActiveInHierarchy() || !rect->ContainsCanvasPoint(pointer, canvasSize)) {
                return;
            }
            // 一覧が開いている間は、一覧の中のスクロールだけを動かす
            if (popupRoot_ && !popupRoot_->IsSelfOrDescendant(rect)) {
                return;
            }
            // 中のスクロールほど後に描くので、描く順の大きい方を採る
            const int order = rect->GetPointerOrder();
            if (!best || order >= bestOrder) {
                best = &view;
                bestOrder = order;
            }
            });

        // 消えた・止められたスクロールは知らせずに手放す（ドラッグの途切れはスクロールの側で気づく）
        if (scrollTarget_ && !targetFound) {
            scrollTarget_ = nullptr;
            scrollDragging_ = false;
        }

        // スクロールの外の、手前の押せる UI の上では動かさない
        if (best && hovered_) {
            const RectTransformComponent* const viewRect = best->GetRectTransform();
            const RectTransformComponent* const hoveredRect = hovered_->GetRectTransform();
            if (hoveredRect && !viewRect->IsSelfOrDescendant(hoveredRect)
                && hovered_->GetPointerSortOrder() > bestOrder) {
                return nullptr;
            }
        }
        return best;
    }

    void UIInteractionFeature::UpdateScroll(UIScrollViewComponent* under, const Vector2& canvasSize,
                                            const Vector2& onCanvas, const UIPointer& pointer,
                                            const InputQuery* query)
    {
        // ホイールはポインタの下のスクロールへ
        if (under && query) {
            const int wheel = query->GetWheelDelta();
            if (wheel != 0) {
                under->Scroll(static_cast<float>(wheel) / kWheelPerNotch);
            }
        }

        // 押した。つまみを掴む UI を押したときは、その UI に任せる
        if (pointer.IsPressed() && under && !scrollTarget_) {
            const bool captured = pressed_ && !pressedByKey_ && pressed_->CapturesDrag();
            if (!captured) {
                scrollTarget_ = under;
                scrollPressPoint_ = onCanvas;
                scrollDragging_ = false;
            }
        }
        if (!scrollTarget_) {
            return;
        }

        if (!pointer.IsHeld()) {
            if (scrollDragging_) {
                scrollTarget_->EndDrag();
            }
            scrollTarget_ = nullptr;
            scrollDragging_ = false;
            return;
        }

        // 押した所から十分に動いたらドラッグにする。押していたボタンは押されなかったことにする
        if (!scrollDragging_) {
            const float dx = onCanvas.x - scrollPressPoint_.x;
            const float dy = onCanvas.y - scrollPressPoint_.y;
            if (std::sqrt(dx * dx + dy * dy) < UIScrollViewComponent::kDragThreshold) {
                return;
            }
            if (pressed_ && !pressedByKey_) {
                pressed_->OnPressEnd(false);
                pressed_ = nullptr;
            }
            scrollTarget_->BeginDrag(scrollPressPoint_, canvasSize);
            scrollDragging_ = true;
        }
        scrollTarget_->Drag(onCanvas, canvasSize);
    }

    void UIInteractionFeature::ScrollIntoView(const IUIInteractable& target)
    {
        const RectTransformComponent* const rect = target.GetRectTransform();
        if (!rect) {
            return;
        }
        // 中のスクロールから外のスクロールの順に、それぞれの窓へ入れる
        const RectTransformComponent* current = rect->GetParent();
        for (int depth = 0; current && depth < RectTransformComponent::kMaxDepth; ++depth) {
            const GameObject* const owner = current->GetOwner();
            UIScrollViewComponent* const view = owner ? owner->GetComponent<UIScrollViewComponent>() : nullptr;
            if (view && view->IsEnabled()) {
                view->ScrollTo(*rect);
            }
            current = current->GetParent();
        }
    }

    void UIInteractionFeature::UpdateNavigation(SceneContext& ctx, const Vector2& canvasSize,
                                                const InputQuery& query)
    {
        if (pressed_) {
            // 押している間は送らない。押しながら選び直すと、どこで離したか分からなくなる
            repeatActive_ = false;
            return;
        }

        UINavigationDirection direction = UINavigationDirection::Up;
        if (!ReadDirection(query, direction)) {
            repeatActive_ = false;
            return;
        }

        bool send = false;
        if (!repeatActive_ || direction != repeatDirection_) {
            // 押した瞬間と、向きを変えた瞬間は待たずに送る
            repeatActive_ = true;
            repeatDirection_ = direction;
            repeatTimer_ = kFirstRepeatDelay;
            send = true;
        }
        else {
            // ポーズ中でもメニューは動かせる必要があるので、止まらない方の時間で測る
            repeatTimer_ -= Time::UnscaledDeltaTime();
            if (repeatTimer_ <= 0.0f) {
                repeatTimer_ = kRepeatInterval;
                send = true;
            }
        }

        if (send) {
            MoveFocus(ctx, canvasSize, direction);
        }
    }

    bool UIInteractionFeature::ReadDirection(const InputQuery& query,
                                             UINavigationDirection& outDirection)
    {
        // アクションの表はエディタから変えられるので、綴りで引き直す
        for (std::size_t i = 0; i < std::size(kDirectionActionIds); ++i) {
            const InputAction action = InputActionFromString(kDirectionActionIds[i]);
            if (action == InputAction::Invalid) {
                continue;
            }
            if (query.IsActionPressed(action)) {
                outDirection = static_cast<UINavigationDirection>(i);
                return true;
            }
        }
        return false;
    }

    void UIInteractionFeature::MoveFocus(SceneContext& ctx, const Vector2& canvasSize,
                                         UINavigationDirection direction)
    {
        if (!focused_) {
            // まだどこも選んでいない。最初の入力では置くだけにする
            // （ポインタが指しているものがあれば、そこから続ける。一覧が開いていれば一覧の最初の相手）
            IUIInteractable* first = hovered_;
            if (popupOwner_ && (!first || !IsPopupFocusCandidate(*first))) {
                first = FindPopupInitialFocus(ctx, canvasSize);
            }
            SetFocus(first ? first : FindFirstFocusable(ctx, canvasSize));
            return;
        }

        // 向きを自分で使う UI（スライダーなど）なら、そちらに譲って送らない
        if (focused_->OnNavigate(direction)) {
            return;
        }

        const RectTransformComponent* const rect = focused_->GetRectTransform();
        if (!rect) {
            return;
        }
        const Vector2 dir = UINavigation::ToVector(direction);
        const Vector2 from = UINavigation::EdgePoint(rect->GetCanvasBounds(canvasSize), dir);

        IUIInteractable* best = nullptr;
        float bestScore = 0.0f;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                if (&candidate == focused_ || !candidate.AcceptsInput()) {
                    return;
                }
                const RectTransformComponent* const candidateRect = candidate.GetRectTransform();
                if (!candidateRect || !candidateRect->IsActiveInHierarchy()) {
                    return;
                }
                if (!IsPopupFocusCandidate(candidate)) {
                    return;
                }
                const Vector2 center =
                    UINavigation::Center(candidateRect->GetCanvasBounds(canvasSize));
                const float score = UINavigation::Score(from, dir, center);
                if (score > bestScore) {
                    bestScore = score;
                    best = &candidate;
                }
            });

        // 向きの先に何も無ければ動かさない（端で止まる）
        if (best) {
            SetFocus(best);
        }
    }

    IUIInteractable* UIInteractionFeature::FindFirstFocusable(SceneContext& ctx,
                                                             const Vector2& canvasSize) const
    {
        IUIInteractable* best = nullptr;
        float bestDistance = 0.0f;

        ctx.gameObjectManager->ForEachComponent<IUIInteractable>(
            [&](IUIInteractable& candidate) {
                if (!candidate.AcceptsInput()) {
                    return;
                }
                const RectTransformComponent* const rect = candidate.GetRectTransform();
                if (!rect || !rect->IsActiveInHierarchy()) {
                    return;
                }
                if (!IsPopupFocusCandidate(candidate)) {
                    return;
                }
                // 画面の左上にいちばん近いものから始める
                const Vector2 center = UINavigation::Center(rect->GetCanvasBounds(canvasSize));
                const float distance = center.x + center.y;
                if (!best || distance < bestDistance) {
                    best = &candidate;
                    bestDistance = distance;
                }
            });

        return best;
    }

    void UIInteractionFeature::SetFocus(IUIInteractable* next)
    {
        if (focused_ == next) {
            return;
        }
        if (focused_) {
            focused_->OnFocusExit();
        }
        focused_ = next;
        if (focused_) {
            focused_->OnFocusEnter();
            ScrollIntoView(*focused_);
        }
    }

    void UIInteractionFeature::ClearAll()
    {
        if (editing_) {
            editing_->EndTextEditing(false);
            editing_ = nullptr;
        }
        if (pressed_) {
            pressed_->OnPressEnd(false);
            pressed_ = nullptr;
        }
        if (hovered_) {
            hovered_->OnPointerExit();
            hovered_ = nullptr;
        }
        SetFocus(nullptr);
        repeatActive_ = false;
        // ドラッグの途切れはスクロールの側で気づいて離す
        scrollTarget_ = nullptr;
        scrollDragging_ = false;
        popupOwner_ = nullptr;
        popupRoot_ = nullptr;
        popupKeyboard_ = false;
    }

    void UIInteractionFeature::Finalize(SceneContext& ctx)
    {
        // シーンと一緒に消えるので、通知を出さずにポインタだけ切る
        RestoreGameContext(ctx);
        pressed_ = nullptr;
        hovered_ = nullptr;
        focused_ = nullptr;
        repeatActive_ = false;
        wasPlaying_ = false;
        scrollTarget_ = nullptr;
        scrollDragging_ = false;
        popupOwner_ = nullptr;
        popupRoot_ = nullptr;
        popupKeyboard_ = false;
        editing_ = nullptr;
    }
}
